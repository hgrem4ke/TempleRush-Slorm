# TempleRush for The Slormancer

*Version française : [README_FR.md](README_FR.md)*

Two things for **The Slorm Temple**: fast clears unlock more floors, and a new Juicy Slorm upgrade removes rooms from every floor.

## Fast clear bonus

The faster you clear a floor, the more floors you unlock.

| Clear time | Floors unlocked |
|------------|-----------------|
| Over 2 minutes | 1 (the game's normal progression) |
| Within 2 minutes | 3 (+2) |
| Within 90 seconds | 4 (+3) |
| Within 1 minute | 5 (+4) |
| Within 30 seconds | 6 (+5) |

For example, if floor 61 is your highest cleared floor and you clear floor 62 in under 2 minutes,
floor 65 becomes available instead of 63.

The bonus only applies when you clear a floor for the first time (a new highest floor). Replaying a
floor you already cleared gives no bonus. The game's executable is never changed.

## Temple Floor Room Count (Juicy Slorm upgrade)

Talk to the Juicy Slorm: a diamond marked **M** appears at the bottom right of the "Personnage" window.
Click it to open the TempleRush page. It lists the mod's upgrades in the style of the game's own list
(more can be added later); click a row to select it, then **Buy**.

**Temple Floor Room Count** removes rooms from every Slorm Temple floor:

- each level is worth 0.1 room, and only whole rooms count: one room is removed every 10 levels, up to
  5 rooms at level 50 (the maximum);
- a floor always keeps at least 3 rooms;
- a level costs 500 x its number, in Pure Slorm.

| Level reached | Rooms removed | Cumulative cost (Pure Slorm) |
|--------------:|--------------:|-----------------------------:|
| 1 | 0 | 500 |
| 10 | 1 | 27,500 |
| 20 | 2 | 105,000 |
| 30 | 3 | 232,500 |
| 40 | 4 | 410,000 |
| 50 | 5 | 637,500 |

The **EN | FR** switch at the top left of the page changes its language (it is remembered; the first
time it follows the language of Windows). Esc, the X button or a click outside the page closes it.

The level is saved **per save slot** in `TempleRush_rooms.txt`, in the game folder (not in the game's own
save file). Copy that file along with your save if you move to another PC.

## Timer on screen

During a Temple floor, a small panel at the top of the screen shows the bonus you can still get and
the time left for it, with a bar that empties. The countdown starts when the first monsters of the
floor appear: until then the panel shows the full time, frozen.

| Panel | Meaning |
|-------|---------|
| `+5  0:23` (gold) | 23 seconds left to clear the floor for +5 floors |
| `+2  1:12` (blue) | 1 min 12 s left to clear the floor for +2 floors |
| `+0  0:00` (grey) | too late, no bonus (disappears after 5 seconds) |
| `+1` (green) | after the clear: the bonus you got (shown for 6 seconds) |

It only shows in the Slorm Temple, not in the Battlefield or the Great Forge.

## How it works

The countdown starts when the first monsters of the floor appear. When the game ends the floor, it records the highest floor
cleared (`slorm_temple_data` in the game, `temple_data` in the save file). TempleRush then adds the
bonus to that value, right before the game saves.

The "Highest floor" text at the top of the Temple menu is the game's own counter and does not include
the bonus. The floors you can pick do.

The timer on screen is drawn through the game's own interface script. If it cannot be hooked,
TempleRush still works, just without the timer (the log says so).

## Installation

You need [Aurie Framework](https://github.com/AurieFramework/Aurie) and
[YYToolkit](https://github.com/AurieFramework/YYToolkit) **v5.0.0c** already installed in the game.

Copy `x64/Release/TempleRush.dll` into `The Slormancer\mods\Aurie\` (game closed).

To uninstall, delete `TempleRush.dll` (and `TempleRush_rooms.txt` if you want to forget the upgrade level).
The floors you unlocked stay unlocked.

## Settings

The times (120, 90, 60 and 30 s), the bonuses (+2, +3, +4 and +5) and the upgrade prices (`kPriceBase`) are fixed
in the code. To change them, edit the constants in `source/ModuleMain.cpp` and rebuild. The only thing saved
is the upgrade level and the page language, in `TempleRush_rooms.txt`.

The timer counts at most 0.25 s per frame, so pausing the game barely counts but does not stop it
completely.

## Building

Visual Studio 2022 or newer, with "Desktop development with C++". Open `TempleRush.vcxproj` and build
**Release / x64**. The Aurie and YYToolkit v5.0.0c headers are in `include/`.

## Changelog

### 1.2.0
- New Juicy Slorm upgrade "Temple Floor Room Count": an **M** diamond in the "Personnage" window opens a page
  where you spend Pure Slorm to remove up to 5 rooms from every Temple floor (level saved per save slot).
- New bonus tiers: under 2 minutes +2, under 90 seconds +3, under 1 minute +4, under 30 seconds +5.
- English / French switch on the page.

### 1.1.0
- Timer on screen: shows the bonus still possible and the time left, with a bar.
- The countdown now starts when the first monsters of the floor appear, not when you enter the floor.
- After a clear, the bonus you got is shown for 6 seconds.

### 1.0.0
- Clear a Slorm Temple floor within 2 minutes: +1 floor unlocked. Within 30 seconds: +5 floors.
- The bonus only applies to a new highest floor.

## License

AGPL-3.0, see [LICENSE](LICENSE). The `include/` folder contains the Aurie and YYToolkit headers
(AGPL-3.0, AurieFramework).

Unofficial mod, not affiliated with the game's developers.
