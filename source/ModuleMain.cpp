// ============================================================
// TempleRush for The Slormancer (Aurie + YYToolkit)
//
// Slorm Temple: clearing a floor quickly unlocks more floors.
//   - clear within 120 s : +2 floors
//   - clear within 90 s  : +3 floors
//   - clear within 60 s  : +4 floors
//   - clear within 30 s  : +5 floors
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
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Aurie;
using namespace YYTK;

// UTF-8 accents for the French text, concatenated so a following letter is never read as part of the escape.
#define FR_E_ACUTE    "\xC3\xA9"
#define FR_E_ACUTE_UP "\xC3\x89"
#define FR_E_GRAVE    "\xC3\xA8"

// ============================================================
// Constants
// ============================================================

// Bonus tiers, from the slowest to the fastest (see FindTier).
struct BonusTier
{
    double seconds;
    int    bonus;
    int    r, g, b;   // timer color
};

static constexpr BonusTier kTiers[] = {
    { 120.0, 2,  90, 200, 255 },
    {  90.0, 3, 120, 255, 200 },
    {  60.0, 4, 255, 170,  60 },
    {  30.0, 5, 255, 215,   0 },
};
static constexpr size_t kTierCount = sizeof(kTiers) / sizeof(kTiers[0]);
static constexpr double kLastTierSeconds = kTiers[0].seconds;   // the slowest threshold

// Tier reached by a floor cleared in Seconds: the fastest one whose time is not exceeded.
static const BonusTier* FindTier(double Seconds)
{
    const BonusTier* found = nullptr;
    for (size_t i = 0; i < kTierCount; i++)
    {
        if (Seconds <= kTiers[i].seconds)
            found = &kTiers[i];
    }
    return found;
}

// Time at which the next faster tier ends (0 for the fastest tier): the start of this tier's window.
static double TierFloorSeconds(const BonusTier* Tier)
{
    return (Tier == &kTiers[kTierCount - 1]) ? 0.0 : (Tier + 1)->seconds;
}

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

static void RunRoomUi();
static void DrawPageIfOpen(int Tag);

static void OnEndStep()
{
    RunRoomUi();

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

    if (std::strcmp(code->GetName(), "gml_Object_obj_ui_next_gen_Draw_64") == 0)
    {
        if (!Context.CalledOriginal())
            Context.Call();
        DrawPageIfOpen(1);
        return;
    }

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

        if (const BonusTier* tier = FindTier(seconds))
        {
            const int left = static_cast<int>(std::ceil(tier->seconds - seconds));
            const double start = TierFloorSeconds(tier);
            std::snprintf(text, sizeof(text), "+%d   %d:%02d", tier->bonus, left / 60, left % 60);
            DrawPanel(text, (tier->seconds - seconds) / (tier->seconds - start), tier->r, tier->g, tier->b);
        }
        else if (seconds <= kLastTierSeconds + kExpiredSeconds)
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
    DrawPageIfOpen(2);
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

    const BonusTier* tier = FindTier(seconds);
    const int bonus = tier ? tier->bonus : 0;

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
// Shorter floors: the Juicy Slorm upgrade "Temple Floor Room Count".
// Each level is worth 0.1 room, and only whole rooms count: 10 levels remove 1 room, 50 levels remove 5.
// ============================================================

static constexpr int    kRoomUpgradeMaxLevel = 50;
static constexpr double kRoomsPerLevel = 0.1;
static constexpr size_t kMinRoomsKept = 3;

static int g_RoomUpgradeLevel = 0;
static void SyncSaveSlot();   // follows the save slot, defined with the save file code

// Whole rooms removed by the current upgrade level.
static size_t RoomsToRemove()
{
    int level = g_RoomUpgradeLevel;
    if (level < 0) level = 0;
    if (level > kRoomUpgradeMaxLevel) level = kRoomUpgradeMaxLevel;
    return static_cast<size_t>(std::floor(level * kRoomsPerLevel + 1e-9));
}

static PFUNC_YYGMLScript g_OriginalGenBsp = nullptr;

// The BSP script returns the rooms of the floor as [ x, y, w, h, "", index ] entries. Remove the rooms
// just before the last one, and put the last room where the first removed one was, so the chain of rooms
// stays connected. At least kMinRoomsKept rooms are always left.
static void DropRoomsBeforeLast(RValue& Rooms)
{
    SyncSaveSlot();
    const size_t wanted = RoomsToRemove();
    if (wanted == 0 || !Rooms.IsArray())
        return;

    std::vector<RValue> rooms = Rooms.ToVector();
    const size_t count = rooms.size();
    if (count <= kMinRoomsKept || !rooms[count - 1].IsArray())
        return;

    const size_t removeCount = (count - kMinRoomsKept < wanted) ? count - kMinRoomsKept : wanted;
    const size_t firstRemoved = count - 1 - removeCount;

    if (!rooms[firstRemoved].IsArray())
        return;

    std::vector<RValue> spot = rooms[firstRemoved].ToVector();
    if (spot.size() < 2)
        return;

    RValue& last = rooms[count - 1];
    g_Yytk->CallBuiltin("array_set", { last, RValue(0.0), RValue(ReadNumber(spot[0])) });
    g_Yytk->CallBuiltin("array_set", { last, RValue(1.0), RValue(ReadNumber(spot[1])) });
    g_Yytk->CallBuiltin("array_set", { last, RValue(5.0), RValue(static_cast<double>(count - removeCount)) });
    g_Yytk->CallBuiltin("array_delete", { Rooms, RValue(static_cast<double>(firstRemoved)), RValue(static_cast<double>(removeCount)) });
}

static RValue& GenBspHook(
    IN CInstance* Self,
    IN CInstance* Other,
    OUT RValue& Result,
    IN int ArgumentCount,
    IN RValue* Arguments[]
)
{
    RValue& ret = g_OriginalGenBsp(Self, Other, Result, ArgumentCount, Arguments);
    DropRoomsBeforeLast(ret);
    return ret;
}

// ============================================================
// Upgrade "Temple Floor Room Count", sold on a page opened by an "M" icon in the Juicy Slorm's window.
// Each level costs Pure Slorm (global.pure_slorm) and is worth 0.1 room; the level is saved in a file next
// to the game.
// ============================================================

static constexpr const char* kRoomSaveFile = "TempleRush_rooms.txt";
static constexpr const char* kCurrencyVar = "pure_slorm";
static constexpr double kPriceBase = 500.0;           // price of the next level = kPriceBase * (level + 1)

static bool g_French = false;

// The level is stored per save slot (the game's global.save_file), in TempleRush_rooms.txt:
//   language=0|1          (0 = English, 1 = French; absent: the language of Windows)
//   level_<slot>=<level>
static std::map<int, int> g_SlotLevels;
static int g_CurrentSlot = -1;

static int ReadSaveSlot()
{
    if (!IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "save_file" })))
        return 0;
    return static_cast<int>(ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { "save_file" })));
}

static void LoadRoomLevel()
{
    std::ifstream file(kRoomSaveFile);
    std::string line;
    while (std::getline(file, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        const std::string key = line.substr(0, eq);
        const int value = std::atoi(line.c_str() + eq + 1);
        if (key == "language")
            g_French = (value == 1);
        else if (key.rfind("level_", 0) == 0)
            g_SlotLevels[std::atoi(key.c_str() + 6)] = value < 0 ? 0 : (value > kRoomUpgradeMaxLevel ? kRoomUpgradeMaxLevel : value);
    }
}

static void SaveRoomLevel()
{
    if (g_CurrentSlot >= 0)
        g_SlotLevels[g_CurrentSlot] = g_RoomUpgradeLevel;

    std::ofstream file(kRoomSaveFile, std::ios::trunc);
    file << "language=" << (g_French ? 1 : 0) << std::endl;
    for (const auto& kv : g_SlotLevels)
        file << "level_" << kv.first << "=" << kv.second << std::endl;
}

// Follows the save the player is on: puts the level of the current slot in g_RoomUpgradeLevel.
static void SyncSaveSlot()
{
    const int slot = ReadSaveSlot();
    if (slot == g_CurrentSlot)
        return;

    if (g_CurrentSlot >= 0)
        g_SlotLevels[g_CurrentSlot] = g_RoomUpgradeLevel;

    g_CurrentSlot = slot;
    const auto it = g_SlotLevels.find(slot);
    g_RoomUpgradeLevel = (it != g_SlotLevels.end()) ? it->second : 0;
}

static double RoomPrice()
{
    return kPriceBase * (g_RoomUpgradeLevel + 1);
}

static const char* Tr(const char* English, const char* French)
{
    return g_French ? French : English;
}

// Buys one level if there are enough Pure Slorm. Returns true if it was bought.
static bool BuyRoomLevel()
{
    SyncSaveSlot();
    if (g_RoomUpgradeLevel >= kRoomUpgradeMaxLevel)
        return false;

    const double gold = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { kCurrencyVar }));
    const double price = RoomPrice();
    if (gold < price)
        return false;

    g_Yytk->CallBuiltin("variable_global_set", { kCurrencyVar, RValue(gold - price) });
    g_RoomUpgradeLevel++;
    SaveRoomLevel();
    return true;
}

// ------------------------------------------------------------
// Mouse: the cursor according to Windows, mapped to GUI coordinates (like the game's own menus do).
// ------------------------------------------------------------

struct MouseState
{
    double x = 0.0;
    double y = 0.0;
    bool pressed = false;   // left button just pressed
    bool focus = false;     // the game window is in the foreground
};

static HWND GetGameWindow()
{
    static HWND cached = nullptr;
    if (cached && IsWindow(cached))
        return cached;

    cached = FindWindowA(nullptr, "The Slormancer");
    if (cached)
        return cached;

    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg)
        GetWindowThreadProcessId(fg, &pid);
    return (fg && pid == GetCurrentProcessId()) ? fg : nullptr;
}

static MouseState ReadMouse(double W, double H)
{
    MouseState m;
    m.x = ReadNumber(g_Yytk->CallBuiltin("device_mouse_x_to_gui", { RValue(0.0) }));
    m.y = ReadNumber(g_Yytk->CallBuiltin("device_mouse_y_to_gui", { RValue(0.0) }));

    HWND hwnd = GetGameWindow();
    if (!hwnd)
    {
        m.pressed = IsTrue(g_Yytk->CallBuiltin("mouse_check_button_pressed", { RValue(1.0) }));
        return m;
    }

    POINT p;
    RECT rc;
    if (GetCursorPos(&p) && ScreenToClient(hwnd, &p) && GetClientRect(hwnd, &rc))
    {
        const double cw = static_cast<double>(rc.right - rc.left);
        const double ch = static_cast<double>(rc.bottom - rc.top);
        if (cw > 0.0 && ch > 0.0)
        {
            m.x = p.x * W / cw;
            m.y = p.y * H / ch;
        }
    }

    static bool wasDown = false;
    m.focus = (GetForegroundWindow() == hwnd);
    const bool down = m.focus && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    m.pressed = down && !wasDown;
    wasDown = down;
    return m;
}

static bool Inside(const MouseState& M, double X1, double Y1, double X2, double Y2)
{
    return M.x >= X1 && M.x <= X2 && M.y >= Y1 && M.y <= Y2;
}

// ------------------------------------------------------------
// The "M" icon in the Juicy Slorm's window, and the page it opens.
// ------------------------------------------------------------

// The M icon sits in the bottom bar of the "Personnage" window, right of the red arrow.
// Position as a fraction of the screen. It is drawn on the screen itself, after the game's menu.
static constexpr double kIconCenterX = 0.8325;
static constexpr double kIconCenterY = 0.9500;
static constexpr double kIconSize = 0.046;     // fraction of the screen height

static PFUNC_YYGMLScript g_OrigPanel = nullptr;      // scr_mage_panel_upgrades: drawn while the NPC's list is shown
static PFUNC_YYGMLScript g_OrigInventory = nullptr;  // scr_ui_menus_inventory: draws the "Personnage" window
static PFUNC_YYGMLScript g_OrigManager = nullptr;    // scr_ui_menus_manager: the game's menu input

static ULONGLONG  g_PanelSeenAt = 0;
static bool       g_PageWasOpen = false;
static bool       g_PageOpen = false;
static ULONGLONG  g_PageOpenedAt = 0;
static bool       g_EscWasDown = false;
static MouseState g_LastMouse;
static double     g_LastW = 0.0, g_LastH = 0.0;

static bool NpcListShown()
{
    return GetTickCount64() - g_PanelSeenAt < 250;
}

static void RunRoomUi();

// Called every frame while the NPC's list is drawn: tells us the list is on screen.
static RValue& PanelHook(IN CInstance* Self, IN CInstance* Other, OUT RValue& Result, IN int ArgumentCount, IN RValue* Arguments[])
{
    g_PanelSeenAt = GetTickCount64();
    return g_OrigPanel(Self, Other, Result, ArgumentCount, Arguments);
}

// True while the "Personnage" window (menu_show = "inventory") is fully shown. Unlike the list's own
// script, this stays true while our page is open and the game's menu input is blocked.
// The game draws its windows into a buffer it only redraws when something changes: ask for a redraw.
static void ForceMenuRedraw()
{
    RValue obj = g_Yytk->CallBuiltin("asset_get_index", { "obj_ui_next_gen" });
    if (ReadNumber(g_Yytk->CallBuiltin("instance_number", { obj })) < 1.0)
        return;

    RValue inst = g_Yytk->CallBuiltin("instance_find", { obj, RValue(0.0) });
    g_Yytk->CallBuiltin("variable_instance_set", { inst, "bake_again", RValue(1.0) });
    g_Yytk->CallBuiltin("variable_instance_set", { inst, "bake_next_frame", RValue(1.0) });
}

static bool g_NpcWindowOpen = false;   // updated once per frame by RunRoomUi
static int  g_ExtraRedraws = 0;         // frames left to ask the game to redraw its window

// True while the "Personnage" window is fully shown (menu_show = "inventory"). With Citizen = true, also
// requires the Juicy Slorm to be the one being talked to (menu_citizen = "mage"). Unlike the list's own
// script, this stays true while our page is open and the game's menu input is blocked.
// The game keeps a value (bake_old_scale) describing the screen its windows were last drawn for. Changing it
// makes the game draw them again, which refreshes what its own window shows (the Slorm amount, ...).
static void ForceFullRebake()
{
    RValue obj = g_Yytk->CallBuiltin("asset_get_index", { "obj_ui_next_gen" });
    if (ReadNumber(g_Yytk->CallBuiltin("instance_number", { obj })) < 1.0)
        return;

    RValue inst = g_Yytk->CallBuiltin("instance_find", { obj, RValue(0.0) });
    g_Yytk->CallBuiltin("variable_instance_set", { inst, "bake_old_scale", RValue(0.0) });
}

static bool InventoryWindowOpen(bool Citizen)
{
    RValue obj = g_Yytk->CallBuiltin("asset_get_index", { "obj_ui_next_gen" });
    if (ReadNumber(g_Yytk->CallBuiltin("instance_number", { obj })) < 1.0)
        return false;

    RValue inst = g_Yytk->CallBuiltin("instance_find", { obj, RValue(0.0) });
    const std::string show(g_Yytk->CallBuiltin("variable_instance_get", { inst, "menu_show" }).ToString());
    const double y = ReadNumber(g_Yytk->CallBuiltin("variable_instance_get", { inst, "menu_y_pos" }));
    if (show != "inventory" || y <= -20.0)
        return false;

    if (!Citizen)
        return true;

    const std::string citizen(g_Yytk->CallBuiltin("variable_instance_get", { inst, "menu_citizen" }).ToString());
    return citizen == "mage";
}

// While our page is open the game does not see the mouse: clicks never reach its menus.
static RValue& ManagerHook(IN CInstance* Self, IN CInstance* Other, OUT RValue& Result, IN int ArgumentCount, IN RValue* Arguments[])
{
    if (g_PageOpen)
        return Result;
    return g_OrigManager(Self, Other, Result, ArgumentCount, Arguments);
}

// ------------------------------------------------------------
// The list of upgrades sold on the page. To add one: write its functions and add a line to kUpgrades.
// ------------------------------------------------------------

struct UpgradeDef
{
    const char* NameEn;
    const char* NameFr;
    const char* TipEn;
    const char* TipFr;
    int    (*Level)();
    int    (*MaxLevel)();
    double (*Price)();                              // price of the next level
    bool   (*Buy)();                                // takes the price and adds a level
    void   (*Value)(char* Out, size_t Size);        // grey text after the name, e.g. "(-4.5)"
};

static int RoomsLevel() { return g_RoomUpgradeLevel; }
static int RoomsMaxLevel() { return kRoomUpgradeMaxLevel; }
static void RoomsValue(char* Out, size_t Size)
{
    std::snprintf(Out, Size, "(-%.1f)", g_RoomUpgradeLevel * kRoomsPerLevel);
}

static const UpgradeDef kUpgrades[] = {
    { "-0.1 Temple Floor Room Count",
      "-0,1 Pi" FR_E_GRAVE "ce par " FR_E_ACUTE "tage du Temple",
      "Reduces the total number of rooms to reach end of floor in The Slorm Temple.",
      "R" FR_E_ACUTE "duit le nombre total de pi" FR_E_GRAVE "ces pour atteindre la fin d'un " FR_E_ACUTE "tage du Temple du Slorm.",
      RoomsLevel, RoomsMaxLevel, RoomPrice, BuyRoomLevel, RoomsValue },
};
static constexpr int kUpgradeCount = static_cast<int>(sizeof(kUpgrades) / sizeof(kUpgrades[0]));
static int g_SelectedUpgrade = 0;

// Page layout, in units of one 1080th of the screen height (so it scales with the resolution).
struct PageLayout
{
    double u;
    double left, top, right, bottom, cx;
    double headerL, headerT, headerR, headerB;
    double pillL, pillT, pillR, pillB;
    double rowsTop, rowHeight, rowGap;
    double tipL, tipT, tipR, tipB;
    double panelL, panelT, panelR, panelB;
    double buyL, buyT, buyR, buyB;
    double closeL, closeT, closeR, closeB;
    double langL, langT, langR, langB;
};

static PageLayout GetPageLayout(double W, double H)
{
    PageLayout l;
    const double u = H / 1080.0;
    l.u = u;
    l.rowHeight = 62.0 * u;
    l.rowGap = 8.0 * u;

    const double width = 760.0 * u;
    const double rowsHeight = kUpgradeCount * (l.rowHeight + l.rowGap);
    const double height = (410.0 * u) + rowsHeight;

    l.left = (W - width) / 2.0;
    l.right = l.left + width;
    l.top = (H - height) / 2.0;
    l.bottom = l.top + height;
    l.cx = (l.left + l.right) / 2.0;

    double y = l.top + 24.0 * u;
    l.headerL = l.left + 24.0 * u; l.headerR = l.right - 24.0 * u;
    l.headerT = y; l.headerB = y + 56.0 * u;
    l.closeR = l.headerR - 6.0 * u; l.closeL = l.closeR - 44.0 * u;
    l.closeT = l.headerT + 6.0 * u; l.closeB = l.closeT + 44.0 * u;
    l.langL = l.headerL + 6.0 * u; l.langR = l.langL + 96.0 * u;
    l.langT = l.closeT; l.langB = l.closeB;

    y = l.headerB + 14.0 * u;
    l.pillL = l.cx - 130.0 * u; l.pillR = l.cx + 130.0 * u;
    l.pillT = y; l.pillB = y + 48.0 * u;

    y = l.pillB + 18.0 * u;
    l.rowsTop = y;
    y += rowsHeight + 6.0 * u;

    l.tipL = l.headerL; l.tipR = l.headerR;
    l.tipT = y; l.tipB = y + 90.0 * u;

    y = l.tipB + 14.0 * u;
    l.panelL = l.headerL; l.panelR = l.headerR;
    l.panelT = y; l.panelB = y + 120.0 * u;

    l.buyL = l.cx - 150.0 * u; l.buyR = l.cx + 150.0 * u;
    l.buyT = l.panelT + 54.0 * u; l.buyB = l.buyT + 56.0 * u;
    return l;
}

static void RowRect(const PageLayout& L, int Index, double& X1, double& Y1, double& X2, double& Y2)
{
    X1 = L.headerL;
    X2 = L.headerR;
    Y1 = L.rowsTop + Index * (L.rowHeight + L.rowGap);
    Y2 = Y1 + L.rowHeight;
}

static void IconRect(double W, double H, double& X1, double& Y1, double& X2, double& Y2)
{
    const double size = kIconSize * H;
    X1 = kIconCenterX * W - size / 2.0;
    Y1 = kIconCenterY * H - size / 2.0;
    X2 = X1 + size;
    Y2 = Y1 + size;
}

// Click area of the icon: the icon itself, a bit larger.
static void IconHitRect(double W, double H, double& X1, double& Y1, double& X2, double& Y2)
{
    IconRect(W, H, X1, Y1, X2, Y2);
    const double grow = 0.1 * (Y2 - Y1);
    X1 -= grow; X2 += grow;
    Y1 -= grow; Y2 += grow;
}

// Text on the page is drawn smaller than the game's own menus.
static constexpr double kPageTextScale = 0.68;

static bool CanBuyUpgrade(const UpgradeDef& Up, double& Gold)
{
    Gold = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { kCurrencyVar }));
    return Up.Level() < Up.MaxLevel() && Gold >= Up.Price();
}

static void FillRect(double X1, double Y1, double X2, double Y2, int R, int G, int B, double Alpha = 1.0)
{
    GmAlpha(Alpha);
    GmColor(R, G, B);
    GmRect(X1, Y1, X2, Y2);
    GmAlpha(1.0);
}

static void FrameRect(double X1, double Y1, double X2, double Y2, double T, int R, int G, int B)
{
    GmAlpha(1.0);
    GmColor(R, G, B);
    GmRect(X1, Y1, X2, Y1 + T);
    GmRect(X1, Y2 - T, X2, Y2);
    GmRect(X1, Y1, X1 + T, Y2);
    GmRect(X2 - T, Y1, X2, Y2);
}

static double TextWidth(const char* Text, double Scale)
{
    return ReadNumber(g_Yytk->CallBuiltin("string_width", { Text })) * Scale * kPageTextScale;
}

static void PageText(double X, double Y, const char* Text, double Scale)
{
    GmText(X, Y, Text, Scale * kPageTextScale);
}

// The small white drop used by the game for Slorm amounts.
static void DrawSlormDrop(double X, double Y, double Radius)
{
    GmColor(235, 245, 255);
    g_Yytk->CallBuiltin("draw_circle", { RValue(X), RValue(Y), RValue(Radius), RValue(0.0) });
}

static void DrawBuyButton(const MouseState& M, const PageLayout& L, bool Enabled, bool Maxed)
{
    const bool hover = Enabled && Inside(M, L.buyL, L.buyT, L.buyR, L.buyB);
    const double u = L.u;
    FillRect(L.buyL, L.buyT, L.buyR, L.buyB, hover ? 40 : 24, hover ? 32 : 20, hover ? 30 : 20);
    FrameRect(L.buyL, L.buyT, L.buyR, L.buyB, 3.0 * u, Enabled ? 110 : 60, Enabled ? 90 : 55, Enabled ? 80 : 50);
    // The red diamonds at both ends, like the game's button.
    const double d = 12.0 * u, my = (L.buyT + L.buyB) / 2.0;
    FillRect(L.buyL + 14.0 * u - d / 2.0, my - d / 2.0, L.buyL + 14.0 * u + d / 2.0, my + d / 2.0, Enabled ? 200 : 80, Enabled ? 70 : 40, Enabled ? 30 : 30);
    FillRect(L.buyR - 14.0 * u - d / 2.0, my - d / 2.0, L.buyR - 14.0 * u + d / 2.0, my + d / 2.0, Enabled ? 200 : 80, Enabled ? 70 : 40, Enabled ? 30 : 30);

    GmAlign(1, 1);
    GmColor(Enabled ? 255 : 130, Enabled ? 255 : 130, Enabled ? 255 : 130);
    PageText(L.cx, my, Maxed ? "MAX" : Tr("Buy", "Acheter"), (u * 1080.0 / 900.0) * 1.15);
    GmAlign(0, 0);
}

static void DrawRoomPage(const MouseState& M, double W, double H)
{
    const PageLayout L = GetPageLayout(W, H);
    const double u = L.u;
    const double ts = H / 900.0;
    char text[200];

    FillRect(0, 0, W, H, 0, 0, 0, 0.55);

    // Window.
    FillRect(L.left, L.top, L.right, L.bottom, 16, 10, 10, 0.98);
    FrameRect(L.left, L.top, L.right, L.bottom, 4.0 * u, 74, 52, 38);
    FrameRect(L.left + 6.0 * u, L.top + 6.0 * u, L.right - 6.0 * u, L.bottom - 6.0 * u, 2.0 * u, 36, 24, 20);

    // Title bar and close button.
    FillRect(L.headerL, L.headerT, L.headerR, L.headerB, 88, 26, 20);
    FrameRect(L.headerL, L.headerT, L.headerR, L.headerB, 2.0 * u, 52, 16, 12);
    GmAlign(1, 1);
    GmColor(160, 118, 56);
    PageText(L.cx, (L.headerT + L.headerB) / 2.0, "TEMPLE RUSH", ts * 1.35);

    // Language switch (EN | FR), like the mods menu's.
    const bool overLang = Inside(M, L.langL, L.langT, L.langR, L.langB);
    FillRect(L.langL, L.langT, L.langR, L.langB, overLang ? 70 : 40, 16, 14);
    FrameRect(L.langL, L.langT, L.langR, L.langB, 2.0 * u, 120, 44, 28);
    GmAlign(1, 1);
    GmColor(g_French ? 140 : 255, g_French ? 120 : 200, g_French ? 100 : 90);
    PageText(L.langL + (L.langR - L.langL) * 0.27, (L.langT + L.langB) / 2.0, "EN", ts * 1.1);
    GmColor(150, 120, 100);
    PageText((L.langL + L.langR) / 2.0, (L.langT + L.langB) / 2.0, "|", ts * 1.1);
    GmColor(g_French ? 255 : 140, g_French ? 200 : 120, g_French ? 90 : 100);
    PageText(L.langL + (L.langR - L.langL) * 0.73, (L.langT + L.langB) / 2.0, "FR", ts * 1.1);

    const bool overClose = Inside(M, L.closeL, L.closeT, L.closeR, L.closeB);
    FillRect(L.closeL, L.closeT, L.closeR, L.closeB, overClose ? 70 : 40, 16, 14);
    FrameRect(L.closeL, L.closeT, L.closeR, L.closeB, 2.0 * u, 120, 44, 28);
    GmColor(overClose ? 255 : 220, overClose ? 120 : 70, 40);
    PageText((L.closeL + L.closeR) / 2.0, (L.closeT + L.closeB) / 2.0, "X", ts * 1.2);

    // Slorm you have.
    double gold = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { kCurrencyVar }));
    FillRect(L.pillL, L.pillT, L.pillR, L.pillB, 8, 6, 6);
    FrameRect(L.pillL, L.pillT, L.pillR, L.pillB, 2.0 * u, 44, 38, 36);
    std::snprintf(text, sizeof(text), "%.0f", gold);
    const double goldWidth = TextWidth(text, ts * 1.1);
    const double goldX = L.cx - (goldWidth + 26.0 * u) / 2.0;
    GmAlign(0, 1);
    GmColor(255, 255, 255);
    PageText(goldX, (L.pillT + L.pillB) / 2.0, text, ts * 1.1);
    DrawSlormDrop(goldX + goldWidth + 16.0 * u, (L.pillT + L.pillB) / 2.0, 7.0 * u);

    // Upgrade rows.
    for (int i = 0; i < kUpgradeCount; i++)
    {
        const UpgradeDef& up = kUpgrades[i];
        double x1, y1, x2, y2;
        RowRect(L, i, x1, y1, x2, y2);
        const bool hover = Inside(M, x1, y1, x2, y2);
        const bool selected = (i == g_SelectedUpgrade);
        const double my = (y1 + y2) / 2.0;

        FillRect(x1, y1, x2, y2, 28, 22, 22, 0.92);
        if (selected) FrameRect(x1, y1, x2, y2, 3.0 * u, 190, 150, 60);
        else if (hover) FrameRect(x1, y1, x2, y2, 2.0 * u, 110, 86, 50);

        // Icon: the game's "square within a square" glyph.
        FrameRect(x1 + 18.0 * u, my - 14.0 * u, x1 + 46.0 * u, my + 14.0 * u, 3.0 * u, 190, 130, 50);
        FillRect(x1 + 28.0 * u, my - 4.0 * u, x1 + 36.0 * u, my + 4.0 * u, 190, 130, 50);

        const char* name = Tr(up.NameEn, up.NameFr);
        GmAlign(0, 1);
        GmColor(255, 255, 255);
        PageText(x1 + 66.0 * u, my, name, ts * 1.0);

        up.Value(text, sizeof(text));
        GmColor(150, 150, 150);
        PageText(x1 + 66.0 * u + TextWidth(name, ts * 1.0) + 14.0 * u, my, text, ts * 0.9);

        char max[24], lvl[24];
        std::snprintf(max, sizeof(max), "/%d", up.MaxLevel());
        std::snprintf(lvl, sizeof(lvl), "%d", up.Level());
        const double maxW = TextWidth(max, ts * 1.0);
        GmAlign(2, 1);
        GmColor(255, 255, 255);
        PageText(x2 - 20.0 * u, my, max, ts * 1.0);
        GmColor(222, 158, 44);
        PageText(x2 - 20.0 * u - maxW, my, lvl, ts * 1.0);
    }

    // Tooltip of the selected upgrade.
    FillRect(L.tipL, L.tipT, L.tipR, L.tipB, 10, 7, 7, 0.9);
    FrameRect(L.tipL, L.tipT, L.tipR, L.tipB, 2.0 * u, 44, 38, 36);
    GmAlign(0, 0);
    GmColor(235, 235, 235);
    const UpgradeDef& sel = kUpgrades[g_SelectedUpgrade];
    g_Yytk->CallBuiltin("draw_text_ext_transformed", { RValue(L.tipL + 16.0 * u), RValue(L.tipT + 14.0 * u),
        Tr(sel.TipEn, sel.TipFr), RValue(-1.0), RValue((L.tipR - L.tipL - 32.0 * u) / (ts * 0.95 * kPageTextScale)), RValue(ts * 0.95 * kPageTextScale), RValue(ts * 0.95 * kPageTextScale), RValue(0.0) });

    // Price and buy button.
    FillRect(L.panelL, L.panelT, L.panelR, L.panelB, 62, 22, 16);
    FrameRect(L.panelL, L.panelT, L.panelR, L.panelB, 2.0 * u, 112, 42, 26);
    const bool maxed = sel.Level() >= sel.MaxLevel();
    const bool canBuy = CanBuyUpgrade(sel, gold);
    FillRect(L.pillL, L.panelT + 10.0 * u, L.pillR, L.panelT + 46.0 * u, 20, 8, 8);
    if (maxed)
    {
        GmAlign(1, 1);
        GmColor(200, 200, 200);
        PageText(L.cx, L.panelT + 28.0 * u, "-", ts * 1.0);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%.0f", sel.Price());
        const double pw = TextWidth(text, ts * 1.0);
        const double px = L.cx - (pw + 26.0 * u) / 2.0;
        GmAlign(0, 1);
        if (gold >= sel.Price()) GmColor(255, 255, 255); else GmColor(230, 80, 60);
        PageText(px, L.panelT + 28.0 * u, text, ts * 1.0);
        DrawSlormDrop(px + pw + 16.0 * u, L.panelT + 28.0 * u, 7.0 * u);
    }
    DrawBuyButton(M, L, canBuy, maxed);

    GmColor(255, 255, 255);
    GmAlpha(1.0);
    GmAlign(0, 0);
}

static void DrawMIcon(const MouseState& M, double W, double H);

// Drawn on the screen after the game's menu (Tag 1: its Draw GUI event, Tag 2: the HUD script).
static void DrawPageIfOpen(int Tag)
{
    if (g_LastW <= 0.0)
        return;

    if (g_PageOpen)
        DrawRoomPage(g_LastMouse, g_LastW, g_LastH);
    else if (g_NpcWindowOpen && Tag == 1)
        DrawMIcon(g_LastMouse, g_LastW, g_LastH);

    GmColor(255, 255, 255);
    GmAlpha(1.0);
    GmAlign(0, 0);
}

static void DrawMIcon(const MouseState& M, double W, double H)
{
    double x1, y1, x2, y2;
    IconRect(W, H, x1, y1, x2, y2);
    const double cx = (x1 + x2) / 2.0, cy = (y1 + y2) / 2.0;
    const double size = y2 - y1;

    double hx1, hy1, hx2, hy2;
    IconHitRect(W, H, hx1, hy1, hx2, hy2);
    const bool hover = Inside(M, hx1, hy1, hx2, hy2);

    // The game's diamond (the one of its Options checkboxes) with an M inside, like the mods menu's button.
    RValue sprite = g_Yytk->CallBuiltin("asset_get_index", { "spr_radio_btn_v3" });
    const double index = ReadNumber(sprite);
    const double w = index >= 0.0 ? ReadNumber(g_Yytk->CallBuiltin("sprite_get_width", { sprite })) : 0.0;
    if (w > 0.0)
    {
        const double h = ReadNumber(g_Yytk->CallBuiltin("sprite_get_height", { sprite }));
        const double xo = ReadNumber(g_Yytk->CallBuiltin("sprite_get_xoffset", { sprite }));
        const double yo = ReadNumber(g_Yytk->CallBuiltin("sprite_get_yoffset", { sprite }));
        const double scale = (size * 1.35) / w;
        g_Yytk->CallBuiltin("draw_sprite_ext",
            { sprite, RValue(hover ? 3.0 : 0.0), RValue(cx - w * 0.5 * scale + xo * scale), RValue(cy - h * 0.5 * scale + yo * scale),
              RValue(scale), RValue(scale), RValue(0.0), RValue(16777215.0), RValue(1.0) });
    }
    else
    {
        GmAlpha(1.0);
        GmColor(hover ? 120 : 62, hover ? 45 : 22, hover ? 22 : 14);
        GmRect(x1, y1, x2, y2);
    }

    GmAlpha(1.0);
    if (hover) GmColor(246, 206, 96); else GmColor(226, 112, 38);
    GmAlign(1, 1);
    GmText(cx, cy, "M", (H / 900.0) * 0.95);
    GmAlign(0, 0);
}

// Drawn inside the "Personnage" window's script, so the icon and the page are on top of the menu
// (anything drawn in the HUD script would be covered by it).
static RValue& InventoryHook(IN CInstance* Self, IN CInstance* Other, OUT RValue& Result, IN int ArgumentCount, IN RValue* Arguments[])
{
    RValue& ret = g_OrigInventory(Self, Other, Result, ArgumentCount, Arguments);

    if (g_PageOpen)
    {
        DrawRoomPage(g_LastMouse, g_LastW, g_LastH);

        GmColor(255, 255, 255);
        GmAlpha(1.0);
        GmAlign(0, 0);
    }
    return ret;
}

// Once per frame while the icon or the page is shown: mouse, clicks and Esc.
static void RunRoomUi()
{
    SyncSaveSlot();
    const double W = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_width", {}));
    const double H = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_height", {}));
    g_LastW = W;
    g_LastH = H;
    g_LastMouse = ReadMouse(W, H);
    const MouseState& M = g_LastMouse;

    g_NpcWindowOpen = InventoryWindowOpen(true);
    if (!g_NpcWindowOpen)
    {
        g_PageOpen = false;
        g_PageWasOpen = false;
        return;
    }

    if (g_ExtraRedraws > 0)
    {
        if (g_ExtraRedraws % 10 == 0)
            ForceFullRebake();
        g_ExtraRedraws--;
        ForceMenuRedraw();
    }

    if (g_PageWasOpen && !g_PageOpen)
        ForceFullRebake();      // the page was just closed
    if (g_PageOpen || g_PageWasOpen)
        ForceMenuRedraw();      // every frame while open, and once more after it closed (to erase it)
    g_PageWasOpen = g_PageOpen;

    const bool escDown = M.focus && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !g_EscWasDown && g_PageOpen)
        g_PageOpen = false;
    g_EscWasDown = escDown;

    if (!M.pressed)
        return;

    if (!g_PageOpen)
    {
        double x1, y1, x2, y2;
        IconHitRect(W, H, x1, y1, x2, y2);
        if (Inside(M, x1, y1, x2, y2))
        {
            g_PageOpen = true;
            g_PageOpenedAt = GetTickCount64();
            ForceMenuRedraw();
        }
        return;
    }

    if (GetTickCount64() - g_PageOpenedAt < 200)   // ignore the click that opened the page
        return;

    const PageLayout L = GetPageLayout(W, H);

    if (Inside(M, L.langL, L.langT, L.langR, L.langB))
    {
        g_French = !g_French;
        SaveRoomLevel();
        return;
    }

    if (Inside(M, L.closeL, L.closeT, L.closeR, L.closeB) || !Inside(M, L.left, L.top, L.right, L.bottom))
    {
        g_PageOpen = false;
        return;
    }

    for (int i = 0; i < kUpgradeCount; i++)
    {
        double x1, y1, x2, y2;
        RowRect(L, i, x1, y1, x2, y2);
        if (Inside(M, x1, y1, x2, y2))
            g_SelectedUpgrade = i;
    }

    if (Inside(M, L.buyL, L.buyT, L.buyR, L.buyB))
    {
        double gold = 0.0;
        const UpgradeDef& sel = kUpgrades[g_SelectedUpgrade];
        if (CanBuyUpgrade(sel, gold) && sel.Buy())
        {
            g_ExtraRedraws = 40;    // refresh the Slorm shown by the game's own window right away
        }
    }
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

    // Shorter floors are optional too.
    if (!installScriptHook("gml_Script_scr_procedural_bsp_create_temple", "GenBsp",
        reinterpret_cast<PVOID>(GenBspHook), g_OriginalGenBsp))
        MmRemoveHook(Module, "GenBsp");

    {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        g_French = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0 && locale[0] == L'f' && locale[1] == L'r';
    }
    LoadRoomLevel();
    if (!installScriptHook("gml_Script_scr_mage_panel_upgrades", "MagePanel", reinterpret_cast<PVOID>(PanelHook), g_OrigPanel))
        MmRemoveHook(Module, "MagePanel");
    if (!installScriptHook("gml_Script_scr_ui_menus_manager", "MenusManager", reinterpret_cast<PVOID>(ManagerHook), g_OrigManager))
        MmRemoveHook(Module, "MenusManager");
    if (!installScriptHook("gml_Script_scr_ui_menus_inventory", "MenusInventory", reinterpret_cast<PVOID>(InventoryHook), g_OrigInventory))
        MmRemoveHook(Module, "MenusInventory");

    for (const BonusTier& tier : kTiers)
        DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] ready: <= %.0f s: +%d floors", tier.seconds, tier.bonus);

    return AURIE_SUCCESS;
}
