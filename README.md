# TempleRush for The Slormancer

*Version française : [README_FR.md](README_FR.md)*

Rewards fast clears in **The Slorm Temple**: the faster you clear a floor, the more floors you unlock.

| Clear time | Floors unlocked |
|------------|-----------------|
| Over 2 minutes | 1 (the game's normal progression) |
| Within 2 minutes | 2 (+1, double progression) |
| Within 30 seconds | 6 (+5, six times the progression) |

For example, if floor 61 is your highest cleared floor and you clear floor 62 in under 2 minutes,
floor 64 becomes available instead of 63.

The bonus only applies when you clear a floor for the first time (a new highest floor). Replaying a
floor you already cleared gives no bonus. The game's executable is never changed.

## Timer on screen

During a Temple floor, a small panel at the top of the screen shows the bonus you can still get and
the time left for it, with a bar that empties. The countdown starts when the first monsters of the
floor appear: until then the panel shows the full time, frozen.

| Panel | Meaning |
|-------|---------|
| `+5  0:23` (gold) | 23 seconds left to clear the floor for +5 floors |
| `+1  1:12` (blue) | 1 min 12 s left to clear the floor for +1 floor |
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

To uninstall, delete `TempleRush.dll`. The floors you unlocked stay unlocked.

## Settings

None: the times (120 s and 30 s) and bonuses (+1 and +5) are fixed in the code. To change them, edit
the constants at the top of `source/ModuleMain.cpp` and rebuild.

The timer counts at most 0.25 s per frame, so pausing the game barely counts but does not stop it
completely.

## Building

Visual Studio 2022 or newer, with "Desktop development with C++". Open `TempleRush.vcxproj` and build
**Release / x64**. The Aurie and YYToolkit v5.0.0c headers are in `include/`.

## Changelog

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
