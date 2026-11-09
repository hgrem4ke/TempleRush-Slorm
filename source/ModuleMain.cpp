// ============================================================
// TempleRush for The Slormancer (Aurie + YYToolkit)
//
// Slorm Temple: clearing a floor quickly unlocks more floors.
//   - clear within 120 s : +1 floor  (double progression)
//   - clear within 30 s  : +5 floors (six times the progression)
//
// The floor timer starts in the game's scr_floor_start. When the game ends the floor
// (scr_floor_end), global.slorm_temple_data[1] (the highest floor cleared, saved as temple_data in
// the save file) is already up to date: right after it, we add the time bonus to that value.
//
// There are no settings: the times and bonuses are the constants below.
// ============================================================

#include <YYToolkit/YYTK_Shared.hpp>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Aurie;
using namespace YYTK;

// ============================================================
// Constants
// ============================================================

static constexpr double kFastSeconds = 120.0;
static constexpr int    kFastBonus = 1;
static constexpr double kUltraSeconds = 30.0;
static constexpr int    kUltraBonus = 5;

// A frame longer than this (pause, loading) is counted as this long in the floor timer.
static constexpr double kMaxFrameSeconds = 0.25;

// global.slorm_temple_data: [ ?, highest floor cleared, ?, ? ]
static constexpr const char* kTempleDataVar = "slorm_temple_data";
static constexpr size_t kHighestFloorIndex = 1;

// ============================================================
// Globals
// ============================================================

static YYTKInterface* g_Yytk = nullptr;
static PFUNC_YYGMLScript g_OriginalFloorStart = nullptr;
static PFUNC_YYGMLScript g_OriginalFloorEnd = nullptr;

static bool   g_FloorRunning = false;
static double g_FloorSeconds = 0.0;
static double g_StartHighest = -1.0;   // slorm_temple_data[1] when the floor started (-1 = unknown)
static LARGE_INTEGER g_LastCounter = {};
static LARGE_INTEGER g_Frequency = {};

// ============================================================
// GameMaker helpers
// ============================================================

static double ReadNumber(RValue Value)
{
    std::string s(Value.ToString());
    return std::atof(s.c_str());
}

static bool IsTrue(RValue Value)
{
    std::string s(Value.ToString());
    if (s == "true" || s == "True") return true;
    return std::atof(s.c_str()) != 0.0;
}

// Reads global.slorm_temple_data as numbers. False if it does not exist or is not an array.
static bool ReadTempleData(std::vector<double>& Out)
{
    Out.clear();

    if (!IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { kTempleDataVar })))
        return false;

    RValue data = g_Yytk->CallBuiltin("variable_global_get", { kTempleDataVar });
    if (!data.IsArray())
        return false;

    for (RValue& entry : data.ToVector())
        Out.push_back(ReadNumber(entry));

    return Out.size() > kHighestFloorIndex;
}

// Writes global.slorm_temple_data. First into the game's own array, so every part of the game
// holding that array (the floor menu included) sees the new value; if that did not stick, by
// replacing the global with a new array.
static void WriteTempleData(const std::vector<double>& Values)
{
    RValue current = g_Yytk->CallBuiltin("variable_global_get", { kTempleDataVar });
    if (current.IsArray())
    {
        for (size_t i = 0; i < Values.size(); ++i)
            g_Yytk->CallBuiltin("array_set", { current, RValue(static_cast<double>(i)), RValue(Values[i]) });

        std::vector<double> check;
        if (ReadTempleData(check) && check.size() == Values.size()
            && check[kHighestFloorIndex] == Values[kHighestFloorIndex])
            return;
    }

    RValue data = g_Yytk->CallBuiltin("array_create", { RValue(static_cast<double>(Values.size())) });

    for (size_t i = 0; i < Values.size(); ++i)
        g_Yytk->CallBuiltin("array_set", { data, RValue(static_cast<double>(i)), RValue(Values[i]) });

    g_Yytk->CallBuiltin("variable_global_set", { kTempleDataVar, data });
}

// ============================================================
// Floor timer: counted in the controller's End Step, so pauses barely count.
// ============================================================

static void OnEndStep()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    double dt = static_cast<double>(now.QuadPart - g_LastCounter.QuadPart) / static_cast<double>(g_Frequency.QuadPart);
    g_LastCounter = now;

    if (!g_FloorRunning)
        return;

    if (dt > kMaxFrameSeconds)
        dt = kMaxFrameSeconds;
    if (dt > 0.0)
        g_FloorSeconds += dt;
}

static void CodeEventCallback(FWCodeEvent& Context)
{
    CCode* code = std::get<2>(Context.Arguments());
    if (!code || !code->GetName())
        return;

    if (std::strcmp(code->GetName(), "gml_Object_obj_controller_Step_2") != 0)
        return;

    if (!Context.CalledOriginal())
        Context.Call();

    OnEndStep();
}

// ============================================================
// Script hooks
// ============================================================

// A floor starts: the timer restarts.
static RValue& FloorStartHook(
    IN CInstance* Self,
    IN CInstance* Other,
    OUT RValue& Result,
    IN int ArgumentCount,
    IN RValue* Arguments[]
)
{
    RValue& ret = g_OriginalFloorStart(Self, Other, Result, ArgumentCount, Arguments);

    g_FloorRunning = true;
    g_FloorSeconds = 0.0;
    QueryPerformanceCounter(&g_LastCounter);

    // The game updates the highest floor before it calls scr_floor_end: remember the value
    // it had when the floor started to tell whether this clear is a new record.
    std::vector<double> data;
    g_StartHighest = ReadTempleData(data) ? data[kHighestFloorIndex] : -1.0;

    return ret;
}

// A floor ends: the game records the floor it just cleared, then we add the time bonus.
static RValue& FloorEndHook(
    IN CInstance* Self,
    IN CInstance* Other,
    OUT RValue& Result,
    IN int ArgumentCount,
    IN RValue* Arguments[]
)
{
    const double seconds = g_FloorSeconds;
    const bool timed = g_FloorRunning;
    g_FloorRunning = false;

    std::vector<double> data;
    const bool hadData = timed && ReadTempleData(data);
    const double beforeCall = hadData ? data[kHighestFloorIndex] : 0.0;
    const double before = (g_StartHighest >= 0.0) ? g_StartHighest : beforeCall;

    RValue& ret = g_OriginalFloorEnd(Self, Other, Result, ArgumentCount, Arguments);

    if (!timed)
        return ret;

    if (!hadData || !ReadTempleData(data))
    {
        DbgPrintEx(LOG_SEVERITY_WARNING, "[TempleRush] global.%s not found: no bonus", kTempleDataVar);
        return ret;
    }

    int bonus = 0;
    if (seconds <= kUltraSeconds)
        bonus = kUltraBonus;
    else if (seconds <= kFastSeconds)
        bonus = kFastBonus;

    const double after = data[kHighestFloorIndex];

    // Only when the game really moved the highest floor up (a floor already cleared gets no bonus).
    if (bonus > 0 && after > before)
    {
        data[kHighestFloorIndex] = after + bonus;
        WriteTempleData(data);

        DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] floor cleared in %.1f s: highest floor %.0f -> %.0f (game +%.0f, bonus +%d)",
            seconds, before, after + bonus, after - before, bonus);
    }
    else
    {
        DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] floor cleared in %.1f s: no bonus (highest floor %.0f -> %.0f)",
            seconds, before, after);
    }

    return ret;
}

// ============================================================
// Module initialization
// ============================================================

EXPORTED AurieStatus ModuleInitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
)
{
    UNREFERENCED_PARAMETER(ModulePath);

    g_Yytk = YYTK::GetInterface();

    if (!g_Yytk)
    {
        OutputDebugStringA("[TempleRush] ERROR: YYToolkit interface not found\n");
        return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    }

    QueryPerformanceFrequency(&g_Frequency);
    QueryPerformanceCounter(&g_LastCounter);

    AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, reinterpret_cast<PVOID>(CodeEventCallback), 0);
    if (!AurieSuccess(status))
    {
        DbgPrintEx(LOG_SEVERITY_ERROR, "[TempleRush] could not register the event callback (%s)", AurieStatusToString(status));
        return status;
    }

    auto installScriptHook = [&](const char* scriptName, const char* hookName,
        PVOID hookFunction, PFUNC_YYGMLScript& original) -> bool
        {
            CScript* script = nullptr;
            AurieStatus st = g_Yytk->GetNamedRoutinePointer(scriptName, reinterpret_cast<PVOID*>(&script));

            if (!AurieSuccess(st) || !script || !script->m_Functions || !script->m_Functions->m_ScriptFunction)
                return false;

            st = MmCreateHook(
                Module,
                hookName,
                reinterpret_cast<PVOID>(script->m_Functions->m_ScriptFunction),
                hookFunction,
                nullptr
            );

            if (!AurieSuccess(st))
                return false;

            original = reinterpret_cast<PFUNC_YYGMLScript>(MmGetHookTrampoline(Module, hookName));
            return original != nullptr;
        };

    const bool okStart = installScriptHook("gml_Script_scr_floor_start", "FloorStart",
        reinterpret_cast<PVOID>(FloorStartHook), g_OriginalFloorStart);
    const bool okEnd = installScriptHook("gml_Script_scr_floor_end", "FloorEnd",
        reinterpret_cast<PVOID>(FloorEndHook), g_OriginalFloorEnd);

    if (!(okStart && okEnd))
    {
        MmRemoveHook(Module, "FloorStart");
        MmRemoveHook(Module, "FloorEnd");

        DbgPrintEx(LOG_SEVERITY_ERROR, "[TempleRush] could not hook scr_floor_start / scr_floor_end");
        return AURIE_OBJECT_NOT_FOUND;
    }

    DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] ready: <= %.0f s: +%d floor, <= %.0f s: +%d floors",
        kFastSeconds, kFastBonus, kUltraSeconds, kUltraBonus);

    return AURIE_SUCCESS;
}
