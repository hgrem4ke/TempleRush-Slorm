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
#include <sstream>
#include <string>
#include <vector>

using namespace Aurie;
using namespace YYTK;

// UTF-8 accents for the French text, concatenated so a following letter is never read as part of the escape.
#define FR_E_ACUTE    "\xC3\xA9"
#define FR_E_ACUTE_UP "\xC3\x89"
#define FR_E_GRAVE    "\xC3\xA8"
#define FR_A_GRAVE    "\xC3\xA0"

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
    { 120.0, 2, 119, 145, 161 },     // +2 blue-grey  (the main color of the game's frame of that rarity)
    {  90.0, 3, 239, 198,  92 },     // +3 gold       (same)
    {  60.0, 4, 236, 127,  18 },     // +4 orange     (same)
    {  30.0, 5, 119,  44, 170 },     // +5 purple     (the Nether frame, #772CAA: also the color of Nether item names)
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
// Fractions of the screen: under the game's "Find the Shrine" line, below the minimap (top right).
static constexpr double kPanelLeftFrac = 0.793;
static constexpr double kPanelWidthFrac = 0.199;
static constexpr double kPanelTopFrac = 0.288;      // just under the game's "Find the Shrine" bar
static constexpr double kPanelHeightFrac = 0.062;

// The countdown freezes once no monster is left for this long (the last room is cleared): the bonus is kept.
static constexpr double kFreezeDelaySeconds = 0.5;
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
static int    g_EnemiesNow = 1;        // monsters seen at the last check
static double g_NoEnemiesSeconds = 0.0;  // how long there has been no monster
static bool   g_TimerFrozen = false;   // no monster left: the countdown is stopped
static bool   g_NearShrine = false;    // the last room (the one with the altar) is loaded
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

// The countdown only freezes in the last room. The monsters appear room by room, so "no monster left" alone
// also happens between two rooms; but the altar (obj_temple_shrine) only exists once the last room is loaded.
static bool LastRoomLoaded()
{
    RValue shrineObj = g_Yytk->CallBuiltin("asset_get_index", { "obj_temple_shrine" });
    return ReadNumber(g_Yytk->CallBuiltin("instance_number", { shrineObj })) >= 1.0;
}

// The portal moves the player by global.next_floor_jump floors (1 by default). After a record, the bonus
// floors are added to it. The variable may not exist yet when the floor ends (it can be created later in
// the game session), so the bonus is kept and applied every frame until the next floor starts.
static int g_PendingJump = 0;
static double g_JumpBase = 1.0;
static bool g_JumpBaseKnown = false;

static void ApplyPendingJump()
{
    if (g_PendingJump <= 0 || !IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "next_floor_jump" })))
        return;

    const double current = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { "next_floor_jump" }));
    if (!g_JumpBaseKnown)
    {
        g_JumpBase = current;
        g_JumpBaseKnown = true;
    }
    const double wanted = g_JumpBase + g_PendingJump;
    if (current != wanted)
        g_Yytk->CallBuiltin("variable_global_set", { "next_floor_jump", RValue(wanted) });
}

static CInstance* g_DrawSelf = nullptr;     // an instance of the game's UI, to run the game's text scripts

static void RunRoomUi();
static void DrawPageIfOpen(int Tag);
static void DrawRoomNotice();

static void OnEndStep()
{
    RunRoomUi();
    ApplyPendingJump();

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
        g_EnemiesNow = enemies;
        DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] first monsters (%d) %.1f s after the floor started: countdown starts",
            enemies, g_WallSeconds);
        return;
    }

    // Once the monsters are gone (the last room is cleared) the countdown stops and the bonus is kept.
    // If monsters appear again (a boss, a new wave) it goes on.
    if (g_EnemyObject >= 0.0)
    {
        static int checkTick = 0;
        if (++checkTick % kEnemyCheckInterval == 0)
            g_EnemiesNow = static_cast<int>(ReadNumber(g_Yytk->CallBuiltin("instance_number", { RValue(g_EnemyObject) })));

        if (g_EnemiesNow <= 0)
            g_NoEnemiesSeconds += dt;
        else
            g_NoEnemiesSeconds = 0.0;

        if (checkTick % kEnemyCheckInterval == 0)
            g_NearShrine = LastRoomLoaded();
    }

    const bool frozen = g_NoEnemiesSeconds >= kFreezeDelaySeconds && g_NearShrine;
    if (frozen != g_TimerFrozen)
    {
        g_TimerFrozen = frozen;
        DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] %s at %.1f s", frozen ? "no monster left: countdown frozen" : "monsters again: countdown goes on", g_FloorSeconds);
    }

    if (!g_TimerFrozen)
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
        g_DrawSelf = std::get<0>(Context.Arguments());
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

// The timer is drawn like the game's objective bar ("Find the Shrine"): its own frame sprite, a colored fill that
// empties as the time runs out, the bonus on the left and the time on the right, in the game's font.
static void GText(double X, double Y, const char* Content, const char* Font, const char* Color, int Align);
static RValue GAsset(const char* Name);
static double UiScale(double H);

// 252 x 44 sprite, on the same column as the game's own bars under the minimap (top right of the screen).
static void TimerBarRect(double& Left, double& Top, double& Width, double& Height, double& Scale)
{
    const double guiWidth = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_width", {}));
    const double guiHeight = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_height", {}));
    Scale = UiScale(guiHeight);
    Width = 252.0 * Scale;
    Height = 60.0 * Scale;
    Left = guiWidth * 0.995 - Width;
    // One bar lower than "Find the Shrine": under the game's bar of Slorm to spend, which it shows below it.
    Top = kPanelTopFrac * guiHeight + 47.0 * Scale;
}

// The frames of the game's item titles (spr_item_tooltip_top, 496 x 168, one frame per rarity): the border changes
// with the bonus, like the rarity of an item. Frame number, and where the picture starts in the sprite.
static void TierFrame(int Bonus, int& Frame, double& TargetY)
{
    switch (Bonus)
    {
    case 2: Frame = 3; TargetY = 52.0; break;      // blue
    case 3: Frame = 4; TargetY = 40.0; break;      // gold (yellow)
    case 4: Frame = 5; TargetY = 44.0; break;      // orange
    case 5: Frame = 8; TargetY = 44.0; break;      // purple (Nether)
    default: Frame = 1; TargetY = 68.0; break;     // plain, for no bonus
    }
}

static const char* Tr(const char* English, const char* French);

// The game's own text script (its fonts are sized for the UI scale, so the letters stay sharp) drawn in white through a
// flat color: gpu fog gives all the text, shadow included, the color of the tier. Centered on X, Y.
static void ColoredText(double X, double Y, const char* Text, const char* Font, int R, int G, int B)
{
    const double color = static_cast<double>(R | (G << 8) | (B << 16));
    g_Yytk->CallBuiltin("gpu_set_fog", { RValue(true), RValue(color), RValue(0.0), RValue(1.0) });
    GText(X, Y, Text, Font, "white", 1);
    g_Yytk->CallBuiltin("gpu_set_fog", { RValue(false), RValue(0.0), RValue(0.0), RValue(1.0) });
}

// The timer: a frame like the title of an item, with "NEXT FLOOR +N  m:ss" centered in it, in the color of the tier.
static void DrawPanel(const char* Text, int Bonus)
{
    double left, top, width, height, scale;
    TimerBarRect(left, top, width, height, scale);

    int frame;
    double targetY;
    TierFrame(Bonus, frame, targetY);
    // Only the title box of the sprite: from where the picture starts down to the line of its origin (138).
    const double partHeight = 138.0 - targetY;
    const double k = width / 408.0;

    g_Yytk->CallBuiltin("draw_sprite_part_ext", { GAsset("spr_item_tooltip_top"), RValue(static_cast<double>(frame)),
        RValue(44.0), RValue(targetY), RValue(408.0), RValue(partHeight), RValue(left), RValue(top), RValue(k), RValue(k),
        RValue(16777215.0), RValue(1.0) });

    int r = 170, g = 170, b = 170;      // no bonus: grey
    for (const BonusTier& tier : kTiers)
        if (tier.bonus == Bonus)
        {
            r = tier.r;
            g = tier.g;
            b = tier.b;
        }

    const double my = top + (partHeight / 2.0 + 12.0) * k;     // a little below the middle of the title box
    ColoredText(left + width / 2.0, my, Text, "ft_standard_14", r, g, b);      // the font of the item titles

    GmColor(255, 255, 255);
    GmAlpha(1.0);
    GmAlign(0, 0);
}

static void DrawTimer()
{
    char text[96];

    if (g_FloorRunning && g_IsTemple)
    {
        const double seconds = g_FloorSeconds;

        if (const BonusTier* tier = FindTier(seconds))
        {
            const int remaining = static_cast<int>(std::ceil(tier->seconds - seconds));
            std::snprintf(text, sizeof(text), "%s +%d   %d:%02d%s", Tr("NEXT FLOOR", "\xC3\x89" "TAGE SUIVANT"), tier->bonus,
                remaining / 60, remaining % 60, g_TimerFrozen ? " OK" : "");      // frozen: the bonus is locked in
            DrawPanel(text, tier->bonus);
        }
        else if (seconds <= kLastTierSeconds + kExpiredSeconds)
        {
            std::snprintf(text, sizeof(text), "%s +0    0:00", Tr("NEXT FLOOR", "\xC3\x89" "TAGE SUIVANT"));
            DrawPanel(text, 0);
        }
        return;
    }

    if (g_ResultBonus > 0 && NowSeconds() < g_ResultUntil)
    {
        std::snprintf(text, sizeof(text), "%s +%d", Tr("NEXT FLOOR", "\xC3\x89" "TAGE SUIVANT"), g_ResultBonus);
        DrawPanel(text, g_ResultBonus);
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
    g_DrawSelf = Self;
    DrawTimer();
    DrawRoomNotice();
    g_DrawSelf = Self;
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
    g_EnemiesNow = 1;
    g_NoEnemiesSeconds = 0.0;
    g_TimerFrozen = false;
    g_NearShrine = false;

    // Without the monsters' parent object there is nothing to wait for: count from the start.
    g_EnemyObject = ReadNumber(g_Yytk->CallBuiltin("asset_get_index", { kEnemyParentObject }));
    g_TimerStarted = g_EnemyObject < 0.0;
    g_IsTemple = IsTempleMode();
    g_ResultBonus = 0;
    g_PendingJump = 0;
    g_JumpBaseKnown = false;
    if (IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "next_floor_jump" })))
        g_Yytk->CallBuiltin("variable_global_set", { "next_floor_jump", RValue(1.0) });
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
    int bonus = tier ? tier->bonus : 0;

    const double after = data[kHighestFloorIndex];

    // The game stops the list of floors at a boss floor (every slorm_temple_boss_every floors, up to
    // slorm_temple_boss_max) until its boss is beaten, and starts a run on the highest floor: a bonus that jumps over
    // a boss floor would leave the player unable to play it. So the bonus stops one floor before the boss floor.
    if (bonus > 0 && after > before)
    {
        const double every = IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "slorm_temple_boss_every" }))
            ? ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { "slorm_temple_boss_every" })) : 50.0;
        const double last = IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "slorm_temple_boss_max" }))
            ? ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { "slorm_temple_boss_max" })) : 150.0;
        for (double boss = every; every >= 1.0 && boss <= last; boss += every)
        {
            if (after < boss && after + bonus >= boss)
            {
                const int allowed = static_cast<int>(boss - 1.0 - after);
                DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] boss floor %.0f: the bonus is cut from +%d to +%d", boss, bonus, allowed);
                bonus = allowed < 0 ? 0 : allowed;
                break;
            }
        }
    }

    // Only when the game really moved the highest floor up (a floor already cleared gets no bonus).
    if (bonus > 0 && after > before)
    {
        data[kHighestFloorIndex] = after + bonus;
        WriteTempleData(data);

        // The portal takes the bonus floors too.
        g_PendingJump = bonus;
        g_JumpBaseKnown = false;
        ApplyPendingJump();

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
// Ten ranks: each one removes 1 room, so rank 10 (the maximum) removes 10 rooms. The price of each rank is in
// kRankPrices (210,297 Pure Slorm in total). The price PAID for each rank is recorded (paid_<slot> in the save
// file) and refunded as paid, so a change of prices in a new version never costs a player anything.
// ============================================================

static constexpr int    kRoomUpgradeMaxLevel = 10;
static constexpr double kRoomsPerLevel = 1.0;
static constexpr double kRankPrices[kRoomUpgradeMaxLevel] = { 921, 1519, 2506, 4134, 6820, 11253, 18566, 30634, 50545, 83399 };
static constexpr size_t kMinRoomsKept = 3;

static int g_RoomUpgradeLevel = 0;
static std::vector<double> g_RoomPaid;   // price paid for each rank of the current slot, in order
static double g_RoomSpent = 0.0;     // Pure Slorm spent on the levels of the current slot

// Notice shown when a floor starts: how many rooms it has, and how many it would have had.
static size_t g_NoticeKept = 0;
static size_t g_NoticeOriginal = 0;
static int g_NoticeKind = 0;            // 0: rooms removed, 1: none (floor already at the minimum), 2: removed down to the minimum
static double g_NoticeUntil = 0.0;
static bool g_NoticePending = false;    // the countdown of the notice starts when it is first drawn
static constexpr double kNoticeSeconds = 8.0;
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
    if (count <= kMinRoomsKept)
    {
        // The upgrade is active but this floor is already at the minimum: tell the player.
        g_NoticeKind = 1;
        g_NoticeOriginal = g_NoticeKept = count;
        g_NoticePending = true;
        return;
    }
    if (!rooms[count - 1].IsArray())
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

    g_NoticeOriginal = count;
    g_NoticeKept = count - removeCount;
    g_NoticeKind = (removeCount < wanted) ? 2 : 0;
    g_NoticePending = true;
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

static bool g_French = false;

// The level is stored per save slot (the game's global.save_file), in TempleRush_rooms.txt:
//   language=0|1          (0 = English, 1 = French; absent: the language of Windows)
//   level_<slot>=<level>
//   spent_<slot>=<Pure Slorm spent on those levels>   (what a refund gives back)
static std::map<int, int> g_SlotLevels;
static std::map<int, double> g_SlotSpent;
static std::map<int, std::vector<double>> g_SlotPaid;
static std::map<int, double> g_SlotRefund;     // Pure Slorm to give back once: old levels that do not make a whole rank
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
    int version = 1;            // files before 1.3.0 have no version: their levels were 0.1 room each (up to 50)
    while (std::getline(file, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        const std::string key = line.substr(0, eq);
        const int value = std::atoi(line.c_str() + eq + 1);
        if (key == "language")
            g_French = (value == 1);
        else if (key == "version")
            version = value;
        else if (key.rfind("level_", 0) == 0)
            g_SlotLevels[std::atoi(key.c_str() + 6)] = value < 0 ? 0 : value;
        else if (key.rfind("paid_", 0) == 0)
        {
            std::vector<double> list;
            const char* at = line.c_str() + eq + 1;
            while (*at)
            {
                char* end = nullptr;
                const double value = std::strtod(at, &end);
                if (end == at)
                    break;
                list.push_back(value);
                at = (*end == ',') ? end + 1 : end;
                if (end == at && *end)
                    break;
            }
            g_SlotPaid[std::atoi(key.c_str() + 5)] = list;
        }
        else if (key.rfind("refund_", 0) == 0)
            g_SlotRefund[std::atoi(key.c_str() + 7)] = std::atof(line.c_str() + eq + 1);
        else if (key.rfind("spent_", 0) == 0)
            g_SlotSpent[std::atoi(key.c_str() + 6)] = std::atof(line.c_str() + eq + 1);
    }

    // RULE for every update: what a player invested is never lost. The Pure Slorm spent are recorded per save
    // (spent_<slot>) and "Refund all" always gives them back, whatever the prices or levels of the new version.
    // Old levels (0.1 room each) become ranks (1 room each): the same whole rooms are removed, and the Slorm
    // already spent stay recorded.
    // The levels that do not make a whole rank (level 7 = 0 whole rooms) are refunded at their old price (50 x level).
    for (auto& kv : g_SlotLevels)
    {
        if (version < 2)
        {
            const int old = kv.second;
            const int lost = old % 10;
            double lostCost = 0.0;
            for (int k = old - lost + 1; k <= old; k++)
                lostCost += 50.0 * k;
            if (lostCost > 0.0)
            {
                g_SlotRefund[kv.first] += lostCost;
                const auto spent = g_SlotSpent.find(kv.first);
                if (spent != g_SlotSpent.end())
                    spent->second = spent->second > lostCost ? spent->second - lostCost : 0.0;
            }
            kv.second = old / 10;
            g_SlotPaid.erase(kv.first);
        }
        if (kv.second > kRoomUpgradeMaxLevel)
            kv.second = kRoomUpgradeMaxLevel;
    }
}

static void SaveRoomLevel()
{
    if (g_CurrentSlot >= 0)
    {
        g_SlotLevels[g_CurrentSlot] = g_RoomUpgradeLevel;
        g_SlotSpent[g_CurrentSlot] = g_RoomSpent;
        g_SlotPaid[g_CurrentSlot] = g_RoomPaid;
    }

    std::ofstream file(kRoomSaveFile, std::ios::trunc);
    file << "version=2" << std::endl;
    file << "language=" << (g_French ? 1 : 0) << std::endl;
    for (const auto& kv : g_SlotLevels)
    {
        file << "level_" << kv.first << "=" << kv.second << std::endl;
        const auto spent = g_SlotSpent.find(kv.first);
        if (spent != g_SlotSpent.end())
            file << "spent_" << kv.first << "=" << static_cast<long long>(spent->second) << std::endl;
        const auto paid = g_SlotPaid.find(kv.first);
        if (paid != g_SlotPaid.end() && !paid->second.empty())
        {
            file << "paid_" << kv.first << "=";
            for (size_t i = 0; i < paid->second.size(); i++)
                file << (i ? "," : "") << static_cast<long long>(paid->second[i]);
            file << std::endl;
        }
        const auto refund = g_SlotRefund.find(kv.first);
        if (refund != g_SlotRefund.end() && refund->second > 0.0)
            file << "refund_" << kv.first << "=" << static_cast<long long>(refund->second) << std::endl;
    }
}

// Follows the save the player is on: puts the level of the current slot in g_RoomUpgradeLevel.
static void SyncSaveSlot()
{
    const int slot = ReadSaveSlot();
    if (slot == g_CurrentSlot)
        return;

    if (g_CurrentSlot >= 0)
    {
        g_SlotLevels[g_CurrentSlot] = g_RoomUpgradeLevel;
        g_SlotSpent[g_CurrentSlot] = g_RoomSpent;
        g_SlotPaid[g_CurrentSlot] = g_RoomPaid;
    }

    g_CurrentSlot = slot;
    const auto paidList = g_SlotPaid.find(slot);
    g_RoomPaid = (paidList != g_SlotPaid.end()) ? paidList->second : std::vector<double>();
    const auto it = g_SlotLevels.find(slot);
    g_RoomUpgradeLevel = (it != g_SlotLevels.end()) ? it->second : 0;

    // A rank saved without its cost (older file): assume it was bought at the normal price.
    const auto spent = g_SlotSpent.find(slot);
    double table = 0.0;
    for (int i = 0; i < g_RoomUpgradeLevel && i < kRoomUpgradeMaxLevel; i++)
        table += kRankPrices[i];
    g_RoomSpent = (spent != g_SlotSpent.end()) ? spent->second : table;
}

static double RoomPrice()
{
    return g_RoomUpgradeLevel < kRoomUpgradeMaxLevel ? kRankPrices[g_RoomUpgradeLevel] : 0.0;
}

static const char* Tr(const char* English, const char* French)
{
    return g_French ? French : English;
}

// What the Room Count upgrade did on this floor, under the timer, for as long as the floor is being played.
static void DrawRoomNotice()
{
    if (g_NoticeOriginal == 0 || !(g_FloorRunning && g_IsTemple))
        return;

    const size_t removed = g_NoticeOriginal > g_NoticeKept ? g_NoticeOriginal - g_NoticeKept : 0;
    char text[200];
    if (g_NoticeKind == 1)
        std::snprintf(text, sizeof(text), Tr("No room removed: already at the minimum (%zu rooms)",
            "Aucune pi" FR_E_GRAVE "ce retir" FR_E_ACUTE "e : d" FR_E_ACUTE "j" FR_A_GRAVE " au minimum (%zu pi" FR_E_GRAVE "ces)"), g_NoticeKept);
    else if (g_NoticeKind == 2)
        std::snprintf(text, sizeof(text), Tr("-%zu room%s: %zu instead of %zu (minimum reached)",
            "-%zu pi" FR_E_GRAVE "ce%s : %zu au lieu de %zu (minimum atteint)"), removed, removed > 1 ? "s" : "", g_NoticeKept, g_NoticeOriginal);
    else
        std::snprintf(text, sizeof(text), Tr("-%zu room%s: %zu instead of %zu",
            "-%zu pi" FR_E_GRAVE "ce%s : %zu au lieu de %zu"), removed, removed > 1 ? "s" : "", g_NoticeKept, g_NoticeOriginal);

    const double guiWidth = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_width", {}));
    const double guiHeight = ReadNumber(g_Yytk->CallBuiltin("display_get_gui_height", {}));
    double barLeft, barTop, barWidth, barHeight, barScale;
    TimerBarRect(barLeft, barTop, barWidth, barHeight, barScale);
    (void)guiWidth;
    (void)guiHeight;

    // The game's own font and text script (sharp letters), in a flat color, centered under the timer's frame.
    ColoredText(barLeft + barWidth / 2.0, barTop + barHeight + 16.0 * barScale, text, "ft_standard_12", 120, 255, 200);
    GmAlpha(1.0);
    GmAlign(0, 0);
    GmColor(255, 255, 255);
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
    g_RoomSpent += price;
    if (g_RoomPaid.size() + 1 == static_cast<size_t>(g_RoomUpgradeLevel))
        g_RoomPaid.push_back(price);
    else
        g_RoomPaid.clear();     // the list does not match the ranks (old save): the table is used for refunds
    SaveRoomLevel();
    return true;
}

// Refund of the last level: what it cost, never more than what was spent in total.
static double RoomRefundOneValue()
{
    SyncSaveSlot();
    if (g_RoomUpgradeLevel <= 0)
        return 0.0;
    double cost = kRankPrices[(g_RoomUpgradeLevel > kRoomUpgradeMaxLevel ? kRoomUpgradeMaxLevel : g_RoomUpgradeLevel) - 1];
    if (g_RoomPaid.size() == static_cast<size_t>(g_RoomUpgradeLevel))
        cost = g_RoomPaid.back();       // what this rank really cost, even if prices changed since
    return cost < g_RoomSpent ? cost : g_RoomSpent;
}

static bool RoomRefundOne()
{
    const double value = RoomRefundOneValue();
    if (g_RoomUpgradeLevel <= 0)
        return false;

    const double gold = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { kCurrencyVar }));
    g_Yytk->CallBuiltin("variable_global_set", { kCurrencyVar, RValue(gold + value) });
    g_RoomUpgradeLevel--;
    if (!g_RoomPaid.empty())
        g_RoomPaid.pop_back();
    g_RoomSpent -= value;
    if (g_RoomSpent < 0.0 || g_RoomUpgradeLevel == 0)
        g_RoomSpent = 0.0;
    SaveRoomLevel();
    return true;
}

// Refund of everything: all the levels go back to 0 and all the Slorm spent come back.
static double RoomRefundAllValue()
{
    SyncSaveSlot();
    return g_RoomSpent > 0.0 ? g_RoomSpent : 0.0;
}

static bool RoomRefundAll()
{
    SyncSaveSlot();
    if (g_RoomUpgradeLevel <= 0 && g_RoomSpent <= 0.0)
        return false;

    const double gold = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { kCurrencyVar }));
    g_Yytk->CallBuiltin("variable_global_set", { kCurrencyVar, RValue(gold + g_RoomSpent) });
    g_RoomUpgradeLevel = 0;
    g_RoomSpent = 0.0;
    g_RoomPaid.clear();
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

// The switch is a small gold-framed box with "Temple rush" in the game's font, on the black square at the top left
// of the NPC's portrait. The frame is the game's spr_support_borders (116 x 68, origin 12,12; frames: 0 gold,
// 1 bright gold, 2 grey). Position and size are in the game's 1920x1080 layout, scaled by its UI scale.
static constexpr double kIconCenterXUnits = 200.0;
static constexpr double kIconCenterYUnits = 199.0;
static constexpr double kIconDrawScale = 0.95;       // of the sprite's own size
static constexpr double kBorderW = 116.0, kBorderH = 68.0, kBorderOrigin = 12.0;

static PFUNC_YYGMLScript g_OrigPanel = nullptr;      // scr_mage_panel_upgrades: drawn while the NPC's list is shown
static PFUNC_YYGMLScript g_OrigInventory = nullptr;  // scr_ui_menus_inventory: draws the "Personnage" window
static PFUNC_YYGMLScript g_OrigManager = nullptr;    // scr_ui_menus_manager: the game's menu input

static ULONGLONG  g_PanelSeenAt = 0;
static bool       g_PageWasOpen = false;
static bool       g_PageOpen = false;
static ULONGLONG  g_PageOpenedAt = 0;
static bool       g_EscWasDown = false;
static ULONGLONG  g_RefundAllArmedUntil = 0;   // a second click before this time confirms "Refund all"
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
    double (*RefundOneValue)();                     // Slorm given back for the last level
    bool   (*RefundOne)();                          // gives them back and removes the level
    double (*RefundAllValue)();                     // Slorm given back for all the levels
    bool   (*RefundAll)();                          // gives them back and removes all the levels
    void   (*Value)(char* Out, size_t Size);        // grey text after the name, e.g. "(-4.5)"
};

static int RoomsLevel() { return g_RoomUpgradeLevel; }
static int RoomsMaxLevel() { return kRoomUpgradeMaxLevel; }
static void RoomsValue(char* Out, size_t Size)
{
    std::snprintf(Out, Size, "(-%d)", static_cast<int>(g_RoomUpgradeLevel * kRoomsPerLevel));
}

static const UpgradeDef kUpgrades[] = {
    { "-1 Temple Floor Room Count",
      "-1 Pi" FR_E_GRAVE "ce par " FR_E_ACUTE "tage du Temple",
      "Reduces the total number of rooms to reach end of floor in The Slorm Temple.",
      "R" FR_E_ACUTE "duit le nombre total de pi" FR_E_GRAVE "ces pour atteindre la fin d'un " FR_E_ACUTE "tage du Temple du Slorm.",
      RoomsLevel, RoomsMaxLevel, RoomPrice, BuyRoomLevel,
      RoomRefundOneValue, RoomRefundOne, RoomRefundAllValue, RoomRefundAll, RoomsValue },
};
static constexpr int kUpgradeCount = static_cast<int>(sizeof(kUpgrades) / sizeof(kUpgrades[0]));
static int g_SelectedUpgrade = 0;

// ------------------------------------------------------------
// The page, in the style of SlormReslormitizer's panel (a mod by wistb, AGPL-3.0, on Nexus Mods): the game's own
// sprites and fonts, laid out on the game's 1920x1080 grid and scaled by the game's UI scale.
// ------------------------------------------------------------

// Sizes in layout units (one unit = UI scale pixels).
static constexpr double kPW = 570.0;        // panel width
static constexpr double kPH = 404.0;        // panel height
static constexpr double kPad = 30.0;
static constexpr double kRow = 38.0;
static constexpr double kArrowW = 24.0, kArrowH = 36.0, kBoxW = 220.0;
static constexpr double kBtnW = 156.0, kBtnH = 52.0, kBtnSpriteW = 168.0;
static constexpr double kLangY = 100.0;
static constexpr double kButtonsY = 292.0;

struct PageLayout
{
    double s;                               // pixels per unit
    double left, top, right, bottom;
    double rowsTop, rowHeight, rowGap;
    double headerL, headerR;
    double buyL, buyT, buyR, buyB;
    double refL, refT, refR, refB;
    double allL, allT, allR, allB;
    double closeL, closeT, closeR, closeB;
    double langL, langT, langR, langB;
};

static double UiScale(double H)
{
    RValue ui = g_Yytk->CallBuiltin("instance_find", { g_Yytk->CallBuiltin("asset_get_index", { "obj_ui_next_gen" }), RValue(0.0) });
    if (IsTrue(g_Yytk->CallBuiltin("variable_instance_exists", { ui, "ui_scale" })))
    {
        const double value = ReadNumber(g_Yytk->CallBuiltin("variable_instance_get", { ui, "ui_scale" }));
        if (value > 0.0)
            return value;
    }
    return H / 1080.0;
}

static PageLayout GetPageLayout(double W, double H)
{
    PageLayout l;
    const double s = UiScale(H);
    l.s = s;
    l.left = (W - kPW * s) / 2.0;
    l.top = (H - kPH * s) / 2.0;
    l.right = l.left + kPW * s;
    l.bottom = l.top + kPH * s;

    auto X = [&](double U) { return l.left + U * s; };
    auto Y = [&](double U) { return l.top + U * s; };

    l.closeL = X(kPW - 74.0); l.closeT = Y(20.0); l.closeR = l.closeL + 52.0 * s; l.closeB = l.closeT + 52.0 * s;

    const double right = kPW - kPad;
    const double langTop = kLangY + (kRow - kArrowH) / 2.0;
    l.langL = X(right - kArrowW - 4.0 - kBoxW - 4.0 - kArrowW); l.langR = X(right);
    l.langT = Y(langTop); l.langB = Y(langTop + kArrowH);

    l.rowsTop = Y(kLangY + kRow);
    l.rowHeight = kRow * s;
    l.rowGap = 0.0;
    l.headerL = X(kPad);
    l.headerR = X(kPW - kPad);

    const double gap = (kPW - 2.0 * kPad - 3.0 * kBtnW) / 2.0;
    l.buyL = X(kPad);                         l.buyR = l.buyL + kBtnW * s;
    l.refL = X(kPad + kBtnW + gap);           l.refR = l.refL + kBtnW * s;
    l.allL = X(kPad + (kBtnW + gap) * 2.0);   l.allR = l.allL + kBtnW * s;
    l.buyT = l.refT = l.allT = Y(kButtonsY);
    l.buyB = l.refB = l.allB = Y(kButtonsY + kBtnH);
    return l;
}

static void RowRect(const PageLayout& L, int Index, double& X1, double& Y1, double& X2, double& Y2)
{
    X1 = L.headerL;
    X2 = L.headerR;
    Y1 = L.rowsTop + Index * L.rowHeight;
    Y2 = Y1 + L.rowHeight;
}

static void IconRect(double W, double H, double& X1, double& Y1, double& X2, double& Y2)
{
    const double scale = UiScale(H);
    const double cx = (W - 1920.0 * scale) / 2.0 + kIconCenterXUnits * scale;
    const double cy = (H - 1080.0 * scale) / 2.0 + kIconCenterYUnits * scale;
    const double w = kBorderW * kIconDrawScale * scale, h = kBorderH * kIconDrawScale * scale;
    X1 = cx - w / 2.0;
    Y1 = cy - h / 2.0;
    X2 = X1 + w;
    Y2 = Y1 + h;
}

// Click area of the icon: the icon itself, a bit larger.
static void IconHitRect(double W, double H, double& X1, double& Y1, double& X2, double& Y2)
{
    IconRect(W, H, X1, Y1, X2, Y2);
    const double grow = 0.05 * (Y2 - Y1);
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

// --- The game's scripts for text, and sprites

static PFUNC_YYGMLScript FindGameScript(const char* Name)
{
    CScript* script = nullptr;
    if (!AurieSuccess(g_Yytk->GetNamedRoutinePointer(Name, reinterpret_cast<PVOID*>(&script))) || !script || !script->m_Functions)
        return nullptr;
    return script->m_Functions->m_ScriptFunction;
}

static RValue RunGameScript(PFUNC_YYGMLScript Function, std::initializer_list<RValue> Arguments)
{
    RValue result;
    if (!Function || !g_DrawSelf)
        return result;
    std::vector<RValue> values(Arguments);
    std::vector<RValue*> pointers;
    for (RValue& value : values)
        pointers.push_back(&value);
    Function(g_DrawSelf, g_DrawSelf, result, static_cast<int>(pointers.size()), pointers.data());
    return result;
}

// Text as the game draws it: fonts and colors by name, sized by the game for its UI scale. Align: 0 left,
// 1 centered, 2 right; Y is the middle of the line.
static void GText(double X, double Y, const char* Content, const char* Font = "ft_standard_12", const char* Color = "white", int Align = 0)
{
    static PFUNC_YYGMLScript drawText = FindGameScript("gml_Script_scr_draw_text");
    RunGameScript(drawText, { RValue(X), RValue(Y), RValue(Content), RValue(1.0), RValue(1.0), RValue(Font), RValue(static_cast<double>(Align)),
        RValue(1.0), RValue(0.0), RValue(1.0), RValue(Color), RValue("black"), RValue(1.0), RValue(false) });
}

// Width and height of text as the game draws it, in layout units.
static double GTextWidth(const char* Content, const char* Font, double S)
{
    static PFUNC_YYGMLScript stringWidth = FindGameScript("gml_Script_scr_string_width");
    return ReadNumber(RunGameScript(stringWidth, { RValue(Content), RValue(Font) })) / S;
}

static double GTextHeight(const char* Font, double S)
{
    static PFUNC_YYGMLScript stringHeight = FindGameScript("gml_Script_scr_string_height");
    return ReadNumber(RunGameScript(stringHeight, { RValue("Ag"), RValue(Font) })) / S;
}

static RValue GAsset(const char* Name)
{
    static std::map<std::string, double> known;
    auto found = known.find(Name);
    if (found == known.end())
        found = known.emplace(Name, ReadNumber(g_Yytk->CallBuiltin("asset_get_index", { Name }))).first;
    return RValue(found->second);
}

static void GSprite(const char* Name, int Frame, double X, double Y, double Scale)
{
    g_Yytk->CallBuiltin("draw_sprite_ext", { GAsset(Name), RValue(static_cast<double>(Frame)), RValue(X), RValue(Y),
        RValue(Scale), RValue(Scale), RValue(0.0), RValue(16777215.0), RValue(1.0) });
}

// A part of a sprite (in its own pixels) drawn at X, Y and stretched to Width x Height pixels.
static void GSpritePart(const char* Name, int Frame, double SL, double ST, double SW, double SH, double X, double Y, double Width, double Height)
{
    g_Yytk->CallBuiltin("draw_sprite_part_ext", { GAsset(Name), RValue(static_cast<double>(Frame)), RValue(SL), RValue(ST), RValue(SW), RValue(SH),
        RValue(X), RValue(Y), RValue(Width / SW), RValue(Height / SH), RValue(16777215.0), RValue(1.0) });
}

static void GRect(double X1, double Y1, double X2, double Y2, int Color)
{
    g_Yytk->CallBuiltin("draw_set_color", { RValue(static_cast<double>(Color)) });
    g_Yytk->CallBuiltin("draw_rectangle", { RValue(X1), RValue(Y1), RValue(X2), RValue(Y2), RValue(false) });
    g_Yytk->CallBuiltin("draw_set_color", { RValue(16777215.0) });
}

// The frame of the character page's weapon box, stretched over the panel: its corners, plain strips of its
// edges and inside, and the skull ornament on top. In the sprite's pixels (419 by 304).
static void DrawGameFrame(const PageLayout& L)
{
    const char* box = "spr_weapon_box_inventory_v2";
    constexpr double SW = 419, SH = 304, CW = 80, CH = 84;
    const double s = L.s;
    const double x1 = L.left, y1 = L.top, x2 = L.right, y2 = L.bottom, w = x2 - x1, h = y2 - y1;
    GRect(x1 + 4 * s, y1 + 17 * s, x2 - 4 * s, y2 - 6 * s, 0x0b0b0b);
    GSpritePart(box, 0, 100, 100, 10, 10, x1 + CW * s, y1 + CH * s, w - CW * s * 2, h - CH * s * 2);
    GSpritePart(box, 0, 72, 0, 8, CH, x1 + CW * s, y1, w - CW * s * 2, CH * s);
    GSpritePart(box, 0, 150, SH - CH, 10, CH, x1 + CW * s, y2 - CH * s, w - CW * s * 2, CH * s);
    GSpritePart(box, 0, 0, CH, CW, 10, x1, y1 + CH * s, CW * s, h - CH * s * 2);
    GSpritePart(box, 0, SW - CW, CH, CW, 10, x2 - CW * s, y1 + CH * s, CW * s, h - CH * s * 2);
    GSpritePart(box, 0, 0, 0, CW, CH, x1, y1, CW * s, CH * s);
    GSpritePart(box, 0, SW - CW, 0, CW, CH, x2 - CW * s, y1, CW * s, CH * s);
    GSpritePart(box, 0, 0, SH - CH, CW, CH, x1, y2 - CH * s, CW * s, CH * s);
    GSpritePart(box, 0, SW - CW, SH - CH, CW, CH, x2 - CW * s, y2 - CH * s, CW * s, CH * s);
    constexpr double OL = 82, OW = 255, OH = 60;
    GSpritePart(box, 0, OL, 0, OW, OH, (x1 + x2 - OW * s) / 2.0, y1, OW * s, OH * s);
}

// The Pure Slorm icon (the game's white-blue drop), centered on X, Y, Size pixels high.
static void DrawPureIcon(double X, double Y, double Size)
{
    const double scale = Size / 64.0;
    g_Yytk->CallBuiltin("draw_sprite_ext", { GAsset("spr_pure_icon"), RValue(0.0), RValue(X), RValue(Y),
        RValue(scale), RValue(scale), RValue(0.0), RValue(16777215.0), RValue(1.0) });
}

// A number followed by the Pure Slorm icon, the pair centered (Align 1) or ending at X (Align 2).
static void GAmount(double X, double Y, const char* Text, const char* Color, int Align, double S)
{
    const double icon = 22.0 * S;
    const double width = GTextWidth(Text, "ft_standard_12", S) * S + 6.0 * S + icon;
    const double left = Align == 1 ? X - width / 2.0 : X - width;
    GText(left, Y, Text, "ft_standard_12", Color, 0);
    DrawPureIcon(left + width - icon / 2.0, Y, icon);
}

static int HoverFrame(bool Over, bool Down)
{
    return Over ? (Down ? 2 : 1) : 0;
}

// The game's grey button: its two ends, narrowed.
static void GButton(const MouseState& M, const PageLayout& L, double X1, double Y1, double X2, double Y2, const char* Label, bool Enabled)
{
    const bool over = Enabled && Inside(M, X1, Y1, X2, Y2);
    const double s = L.s;
    const double half = (kBtnW / 2.0);
    const int frame = Enabled ? HoverFrame(over, false) : 0;
    GSpritePart("spr_menu_grey_button", frame, 0, 0, half, kBtnH, X1, Y1, half * s, kBtnH * s);
    GSpritePart("spr_menu_grey_button", frame, kBtnSpriteW - half, 0, half, kBtnH, X1 + half * s, Y1, half * s, kBtnH * s);
    GText((X1 + X2) / 2.0, (Y1 + Y2) / 2.0, Label, "ft_standard_12", Enabled ? "white" : "grey", 1);
}

// Text broken at spaces into lines no wider than Width (layout units).
static std::vector<std::string> WrapText(const std::string& Content, const char* Font, double Width, double S)
{
    std::vector<std::string> lines;
    std::string line, word;
    std::istringstream words(Content);
    while (words >> word)
    {
        const std::string longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && GTextWidth(longer.c_str(), Font, S) > Width)
        {
            lines.push_back(line);
            line = word;
        }
        else
            line = longer;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

static void DrawRoomPage(const MouseState& M, double W, double H)
{
    const PageLayout L = GetPageLayout(W, H);
    const double s = L.s;
    char text[200];

    auto X = [&](double U) { return L.left + U * s; };
    auto Y = [&](double U) { return L.top + U * s; };
    const double R = kPW - kPad;

    // The game stays visible, darkened, behind the panel.
    FillRect(0, 0, W, H, 0, 0, 0, 0.55);
    DrawGameFrame(L);

    // Title, under the frame's ornament, and the close button.
    GText(X(kPad), Y(73.0), "TempleRush", "ft_standard_16");
    const bool overClose = Inside(M, L.closeL, L.closeT, L.closeR, L.closeB);
    GSprite("spr_menu_escape_button", HoverFrame(overClose, false), L.closeL, L.closeT, s);

    // Language: a selector like the game's, with an arrow on each side of the value.
    GText(X(kPad), Y(kLangY + kRow / 2.0), Tr("Language", "Langue"));
    {
        const double top = Y(kLangY + (kRow - kArrowH) / 2.0);
        const double rightArrow = X(R - kArrowW);
        const double box = rightArrow - 4.0 * s - kBoxW * s;
        const double leftArrow = box - 4.0 * s - kArrowW * s;
        const bool over = Inside(M, L.langL, L.langT, L.langR, L.langB);
        GRect(box, top, box + kBoxW * s, top + kArrowH * s, 0x020202);
        GRect(box + 3 * s, top + 3 * s, box + kBoxW * s - 3 * s, top + kArrowH * s - 3 * s, 0x101010);
        GText(box + kBoxW * s / 2.0, top + kArrowH * s / 2.0, g_French ? "Fran\xC3\xA7" "ais" : "English", "ft_standard_12", "grey", 1);
        GSprite("spr_selector_btn_l_v2", HoverFrame(over, false), leftArrow, top, s);
        GSprite("spr_selector_btn_r_v2", HoverFrame(over, false), rightArrow, top, s);
    }

    // Upgrade rows: the name, what it gives so far, and its level.
    for (int i = 0; i < kUpgradeCount; i++)
    {
        const UpgradeDef& up = kUpgrades[i];
        double x1, y1, x2, y2;
        RowRect(L, i, x1, y1, x2, y2);
        const bool hover = Inside(M, x1, y1, x2, y2);
        const bool selected = (i == g_SelectedUpgrade);
        const double my = (y1 + y2) / 2.0;

        if (selected)
        {
            FillRect(x1 - 6 * s, y1 + 2 * s, x2 + 6 * s, y2 - 2 * s, 24, 20, 14, 1.0);
            FrameRect(x1 - 6 * s, y1 + 2 * s, x2 + 6 * s, y2 - 2 * s, 2 * s, 110, 86, 50);
        }
        else if (hover)
            FrameRect(x1 - 6 * s, y1 + 2 * s, x2 + 6 * s, y2 - 2 * s, 1 * s, 70, 56, 36);

        const char* name = Tr(up.NameEn, up.NameFr);
        GText(x1, my, name, "ft_standard_12", selected ? "item_highlight" : "white");
        up.Value(text, sizeof(text));
        GText(x1 + (GTextWidth(name, "ft_standard_12", s) + 10.0) * s, my, text, "ft_standard_11", "grey");

        char level[40];
        std::snprintf(level, sizeof(level), "%d / %d", up.Level(), up.MaxLevel());
        GText(x2, my, level, "ft_standard_12", "item_highlight", 2);
    }

    // The selected upgrade: what you have, what it does, on the game's dark red background.
    const UpgradeDef& sel = kUpgrades[g_SelectedUpgrade];
    double gold = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { kCurrencyVar }));
    const double boxTop = kLangY + kRow * (1 + kUpgradeCount) + 12.0;
    const double boxBottom = kButtonsY - 34.0;
    FillRect(X(kPad - 4.0), Y(boxTop), X(R + 4.0), Y(boxBottom), 38, 12, 12, 1.0);

    double y = boxTop + 16.0;
    GText(X(kPad + 6.0), Y(y), Tr("Slorm owned", "Slorm poss\xC3\xA9" "d\xC3\xA9s"), "ft_standard_12", "white", 0);
    std::snprintf(text, sizeof(text), "%.0f", gold);
    GAmount(X(R - 6.0), Y(y), text, "white", 2, s);
    y += 22.0;

    const double lineHeight = GTextHeight("ft_standard_11", s) + 2.0;
    const std::vector<std::string> tip = WrapText(Tr(sel.TipEn, sel.TipFr), "ft_standard_11", R - kPad - 12.0, s);
    for (size_t i = 0; i < tip.size() && i < 2; i++)
        GText(X(kPad + 6.0), Y(y + lineHeight * static_cast<double>(i)), tip[i].c_str(), "ft_standard_11", "grey");

    // Price and refunds, over the three buttons, each followed by the Pure Slorm icon.
    const bool maxed = sel.Level() >= sel.MaxLevel();
    const bool canBuy = CanBuyUpgrade(sel, gold);
    const double refundOne = sel.RefundOneValue();
    const double refundAll = sel.RefundAllValue();
    const double captionY = kButtonsY - 14.0;

    if (maxed)
        GText((L.buyL + L.buyR) / 2.0, Y(captionY), "-", "ft_standard_12", "white", 1);
    else
    {
        std::snprintf(text, sizeof(text), "%.0f", sel.Price());
        GAmount((L.buyL + L.buyR) / 2.0, Y(captionY), text, gold >= sel.Price() ? "white" : "grey", 1, s);
    }
    std::snprintf(text, sizeof(text), "+%.0f", refundOne);
    GAmount((L.refL + L.refR) / 2.0, Y(captionY), text, "item_highlight", 1, s);
    std::snprintf(text, sizeof(text), "+%.0f", refundAll);
    GAmount((L.allL + L.allR) / 2.0, Y(captionY), text, "item_highlight", 1, s);

    GButton(M, L, L.buyL, L.buyT, L.buyR, L.buyB, maxed ? "MAX" : Tr("Buy", "Acheter"), canBuy);
    GButton(M, L, L.refL, L.refT, L.refR, L.refB, Tr("Refund", "Rembourser"), refundOne > 0.0 || sel.Level() > 0);
    const bool armed = GetTickCount64() < g_RefundAllArmedUntil;
    GButton(M, L, L.allL, L.allT, L.allR, L.allB, armed ? Tr("Confirm?", "Confirmer ?") : Tr("Refund all", "Tout rembourser"), refundAll > 0.0);

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

// The switch that opens the page: the game's gold border with "Temple rush" inside, in the game's font. It lights up
// under the mouse and while the page is open.
static void DrawMIcon(const MouseState& M, double W, double H)
{
    double x1, y1, x2, y2;
    IconRect(W, H, x1, y1, x2, y2);
    const double scale = UiScale(H);
    const double sc = kIconDrawScale * scale;

    double hx1, hy1, hx2, hy2;
    IconHitRect(W, H, hx1, hy1, hx2, hy2);
    const bool hover = Inside(M, hx1, hy1, hx2, hy2);
    const bool lit = hover || g_PageOpen;

    // Dark inside, then the frame (its origin is 12,12 in the sprite's pixels), then the text.
    FillRect(x1 + 6.0 * sc, y1 + 6.0 * sc, x2 - 6.0 * sc, y2 - 6.0 * sc, 14, 12, 10, 1.0);
    g_Yytk->CallBuiltin("draw_sprite_ext", { GAsset("spr_support_borders"), RValue(lit ? 1.0 : 0.0), RValue(x1 + kBorderOrigin * sc), RValue(y1 + kBorderOrigin * sc),
        RValue(sc), RValue(sc), RValue(0.0), RValue(16777215.0), RValue(1.0) });

    const double cx = (x1 + x2) / 2.0, cy = (y1 + y2) / 2.0;
    const char* color = lit ? "item_highlight" : "white";
    GText(cx, cy - 9.0 * scale, "Temple", "ft_standard_11", color, 1);
    GText(cx, cy + 9.0 * scale, "rush", "ft_standard_11", color, 1);
}

// Drawn inside the "Personnage" window's script, so the icon and the page are on top of the menu
// (anything drawn in the HUD script would be covered by it).
static RValue& InventoryHook(IN CInstance* Self, IN CInstance* Other, OUT RValue& Result, IN int ArgumentCount, IN RValue* Arguments[])
{
    RValue& ret = g_OrigInventory(Self, Other, Result, ArgumentCount, Arguments);

    if (g_PageOpen)
    {
        g_DrawSelf = Self;
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

    // Slorm owed from the conversion of the old levels: given back once, now that the save is loaded.
    {
        const auto owed = g_SlotRefund.find(g_CurrentSlot);
        if (owed != g_SlotRefund.end() && owed->second > 0.0)
        {
            const double gold = ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { kCurrencyVar }));
            g_Yytk->CallBuiltin("variable_global_set", { kCurrencyVar, RValue(gold + owed->second) });
            DbgPrintEx(LOG_SEVERITY_INFO, "[TempleRush] old levels converted: %.0f Pure Slorm given back", owed->second);
            owed->second = 0.0;
            SaveRoomLevel();
        }
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

    const UpgradeDef& sel = kUpgrades[g_SelectedUpgrade];
    const ULONGLONG now = GetTickCount64();

    if (Inside(M, L.buyL, L.buyT, L.buyR, L.buyB))
    {
        double gold = 0.0;
        if (CanBuyUpgrade(sel, gold) && sel.Buy())
            g_ExtraRedraws = 40;    // refresh the Slorm shown by the game's own window right away
        g_RefundAllArmedUntil = 0;
    }
    else if (Inside(M, L.refL, L.refT, L.refR, L.refB))
    {
        if (sel.Level() > 0 && sel.RefundOne())
            g_ExtraRedraws = 40;
        g_RefundAllArmedUntil = 0;
    }
    else if (Inside(M, L.allL, L.allT, L.allR, L.allB))
    {
        if (sel.RefundAllValue() > 0.0)
        {
            if (now < g_RefundAllArmedUntil)
            {
                if (sel.RefundAll())
                    g_ExtraRedraws = 40;
                g_RefundAllArmedUntil = 0;
            }
            else
            {
                g_RefundAllArmedUntil = now + 3000;     // ask for a second click
            }
        }
    }
    else
    {
        g_RefundAllArmedUntil = 0;
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
