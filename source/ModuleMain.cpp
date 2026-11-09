// ============================================================
// TempleRush for The Slormancer (Aurie + YYToolkit)
//
// Slorm Temple: clearing a floor quickly unlocks more floors.
//   - clear within 120 s : +1 floor  (double progression)
//   - clear within 30 s  : +5 floors (six times the progression)
//
// The floor timer starts when the first monsters of the floor appear (the game's enemy parent
// object has an instance), after the game's scr_floor_start. When the game ends the floor
// (scr_floor_end), global.slorm_temple_data[1] (the highest floor cleared, saved as temple_data in
// the save file) is already up to date: right after it, we add the time bonus to that value.
//
// A small timer at the top of the screen shows the time left for the current bonus.
//
// There are no settings: the times and bonuses are the constants below.
// ============================================================

#include <YYToolkit/YYTK_Shared.hpp>
#include <cmath>
#include <cstdio>
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

// Timer on screen: panel size and position (GUI pixels), time the result stays after a clear,
// and how long the "no bonus" state stays once the last threshold has passed.
static constexpr double kPanelWidth = 150.0;
static constexpr double kPanelHeight = 34.0;
static constexpr double kPanelTop = 64.0;
static constexpr double kResultSeconds = 6.0;
static constexpr double kExpiredSeconds = 5.0;

// Parent object of every monster: the timer starts when the first instance of it exists.
static constexpr const char* kEnemyParentObject = "obj_parent_enemy_NL";

// How often (in frames) the timer looks for the first monsters.
static constexpr int kEnemyCheckInterval = 6;

// global.selected_game_mode: [ battlefield, temple, forge ], 1 for the chosen mode.
static constexpr const char* kGameModeVar = "selected_game_mode";
static constexpr size_t kTempleModeIndex = 1;

// global.slorm_temple_data: [ ?, highest floor cleared, ?, ? ]
static constexpr const char* kTempleDataVar = "slorm_temple_data";
static constexpr size_t kHighestFloorIndex = 1;

// ============================================================
// Globals
// ============================================================

static YYTKInterface* g_Yytk = nullptr;
static PFUNC_YYGMLScript g_OriginalFloorStart = nullptr;
static PFUNC_YYGMLScript g_OriginalFloorEnd = nullptr;
static PFUNC_YYGMLScript g_OriginalHud = nullptr;

static bool   g_FloorRunning = false;
static double g_FloorSeconds = 0.0;
static bool   g_TimerStarted = false;  // the first monsters of the floor have appeared
static double g_EnemyObject = -1.0;    // asset index of kEnemyParentObject (-1 = unknown)
static double g_WallSeconds = 0.0;     // real time since the floor started, monsters or not
static bool   g_IsTemple = true;       // the floor being played is a Slorm Temple floor
static int    g_ResultBonus = 0;       // bonus shown after a clear
static double g_ResultUntil = 0.0;     // NowSeconds() until which the result is shown
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

static double NowSeconds()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(g_Frequency.QuadPart);
}

// True when the Slorm Temple is the chosen game mode (true if the game does not say).
static bool IsTempleMode()
{
    if (!IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { kGameModeVar })))
        return true;

    RValue modes = g_Yytk->CallBuiltin("variable_global_get", { kGameModeVar });
    if (!modes.IsArray())
        return true;

    std::vector<RValue> values = modes.ToVector();
    return values.size() <= kTempleModeIndex || ReadNumber(values[kTempleModeIndex]) == 1.0;
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
    if (dt <= 0.0)
        return;

    g_WallSeconds += dt;

    // Waiting for the first monsters: the countdown has not started yet.
    if (!g_TimerStarted)
    {
        static int tick = 0;
        if (++tick % kEnemyCheckInterval != 0)
            return;

        const int enemies = static_cast<int>(ReadNumber(g_Yytk->CallBuiltin("instance_number", { RValue(g_EnemyObject) })));
        if (enemies <= 0)
            return;

        g_TimerStarted = true;
        DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] first monsters (%d) %.1f s after the floor started: countdown starts",
            enemies, g_WallSeconds);
        return;
    }

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
// Timer on screen (Draw GUI)
// ============================================================

static void GmColor(int R, int G, int B)
{
    RValue c = g_Yytk->CallBuiltin("make_color_rgb",
        { RValue(static_cast<double>(R)), RValue(static_cast<double>(G)), RValue(static_cast<double>(B)) });
    g_Yytk->CallBuiltin("draw_set_color", { c });
}

static void GmAlpha(double A)
{
    g_Yytk->CallBuiltin("draw_set_alpha", { RValue(A) });
}

static void GmAlign(int H, int V)   // 0 = left/top, 1 = center, 2 = right/bottom
{
    g_Yytk->CallBuiltin("draw_set_halign", { RValue(static_cast<double>(H)) });
    g_Yytk->CallBuiltin("draw_set_valign", { RValue(static_cast<double>(V)) });
}

static void GmRect(double X1, double Y1, double X2, double Y2)
{
    g_Yytk->CallBuiltin("draw_rectangle", { RValue(X1), RValue(Y1), RValue(X2), RValue(Y2), RValue(0.0) });
}

static void GmText(double X, double Y, const char* Text, double Scale)
{
    g_Yytk->CallBuiltin("draw_text_transformed",
        { RValue(X), RValue(Y), Text, RValue(Scale), RValue(Scale), RValue(0.0) });
}

// One panel: "+N  m:ss" over a bar. Fraction = part of the bar still filled (0 to 1).
static void DrawPanel(const char* Text, double Fraction, int R, int G, int B)
{
    const double guiWidth = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_width", {}));
    const double left = guiWidth / 2.0 - kPanelWidth / 2.0;
    const double right = left + kPanelWidth;
    const double top = kPanelTop;
    const double bottom = top + kPanelHeight;

    GmAlpha(0.6);
    GmColor(0, 0, 0);
    GmRect(left, top, right, bottom);

    GmAlpha(1.0);
    GmColor(R, G, B);
    GmAlign(1, 1);
    GmText(guiWidth / 2.0, top + (kPanelHeight - 6.0) / 2.0, Text, 1.2);

    // Bar along the bottom edge: dark track, then the remaining part.
    GmColor(60, 60, 60);
    GmRect(left + 4.0, bottom - 7.0, right - 4.0, bottom - 3.0);
    if (Fraction > 0.0)
    {
        GmColor(R, G, B);
        GmRect(left + 4.0, bottom - 7.0, left + 4.0 + (kPanelWidth - 8.0) * (Fraction > 1.0 ? 1.0 : Fraction), bottom - 3.0);
    }

    GmColor(255, 255, 255);
    GmAlpha(1.0);
    GmAlign(0, 0);
}

static void DrawTimer()
{
    char text[32];

    if (g_FloorRunning && g_IsTemple)
    {
        const double seconds = g_FloorSeconds;

        if (seconds <= kUltraSeconds)
        {
            const int left = static_cast<int>(std::ceil(kUltraSeconds - seconds));
            std::snprintf(text, sizeof(text), "+%d   %d:%02d", kUltraBonus, left / 60, left % 60);
            DrawPanel(text, (kUltraSeconds - seconds) / kUltraSeconds, 255, 215, 0);
        }
        else if (seconds <= kFastSeconds)
        {
            const int left = static_cast<int>(std::ceil(kFastSeconds - seconds));
            std::snprintf(text, sizeof(text), "+%d   %d:%02d", kFastBonus, left / 60, left % 60);
            DrawPanel(text, (kFastSeconds - seconds) / kFastSeconds, 90, 200, 255);
        }
        else if (seconds <= kFastSeconds + kExpiredSeconds)
        {
            DrawPanel("+0   0:00", 0.0, 150, 150, 150);
        }
        return;
    }

    if (g_ResultBonus > 0 && NowSeconds() < g_ResultUntil)
    {
        std::snprintf(text, sizeof(text), "+%d", g_ResultBonus);
        DrawPanel(text, 1.0, 120, 255, 120);
    }
}

// Draw GUI, once per frame.
static RValue& HudDrawHook(
    IN CInstance* Self,
    IN CInstance* Other,
    OUT RValue& Result,
    IN int ArgumentCount,
    IN RValue* Arguments[]
)
{
    RValue& ret = g_OriginalHud(Self, Other, Result, ArgumentCount, Arguments);
    DrawTimer();
    return ret;
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
    g_WallSeconds = 0.0;

    // Without the monsters' parent object there is nothing to wait for: count from the start.
    g_EnemyObject = ReadNumber(g_Yytk->CallBuiltin("asset_get_index", { kEnemyParentObject }));
    g_TimerStarted = g_EnemyObject < 0.0;
    g_IsTemple = IsTempleMode();
    g_ResultBonus = 0;
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
    // If no monster was ever seen, fall back to the real time since the floor started.
    const double seconds = g_TimerStarted ? g_FloorSeconds : g_WallSeconds;
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

        g_ResultBonus = bonus;
        g_ResultUntil = NowSeconds() + kResultSeconds;

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

    // The timer on screen is optional: the bonus works without it.
    if (!installScriptHook("gml_Script_scr_ui_draw_gold_essence", "HudDraw",
        reinterpret_cast<PVOID>(HudDrawHook), g_OriginalHud))
    {
        MmRemoveHook(Module, "HudDraw");
        DbgPrintEx(LOG_SEVERITY_WARNING, "[TempleRush] could not hook the HUD: no timer on screen");
    }

    DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] ready: <= %.0f s: +%d floor, <= %.0f s: +%d floors",
        kFastSeconds, kFastBonus, kUltraSeconds, kUltraBonus);

    return AURIE_SUCCESS;
}
