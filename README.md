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

## How it works

The timer starts when a floor starts. When the game ends the floor, it records the highest floor
cleared (`slorm_temple_data` in the game, `temple_data` in the save file). TempleRush then adds the
bonus to that value, right before the game saves.

The "Highest floor" text at the top of the Temple menu is the game's own counter and does not include
the bonus. The floors you can pick do.

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

## License

AGPL-3.0, see [LICENSE](LICENSE). The `include/` folder contains the Aurie and YYToolkit headers
(AGPL-3.0, AurieFramework).

Unofficial mod, not affiliated with the game's developers.
