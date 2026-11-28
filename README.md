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

Talk to the Juicy Slorm: the gold-framed **Temple rush** box on the black square at the top left of the NPC's
portrait opens the TempleRush page, in the style of SlormReslormitizer's panel (the game's own frame, selector,
buttons and fonts). It lists the mod's upgrades (more can be added later); click a row to select it, then **Buy**.
The page has its own language selector (English / French).

**Temple Floor Room Count** removes rooms from every Slorm Temple floor:

- 10 ranks: each rank removes 1 room, so rank 10 (the maximum) removes 10 rooms;
- a floor always keeps at least 3 rooms;
- each rank has its own price, in Pure Slorm (210,297 in total):

| Rank | Price (Pure Slorm) | Cumulative cost |
|--:|--:|--:|
| 1 | 921 | 921 |
| 2 | 1,519 | 2,440 |
| 3 | 2,506 | 4,946 |
| 4 | 4,134 | 9,080 |
| 5 | 6,820 | 15,900 |
| 6 | 11,253 | 27,153 |
| 7 | 18,566 | 45,719 |
| 8 | 30,634 | 76,353 |
| 9 | 50,545 | 126,898 |
| 10 | 83,399 | 210,297 |

**Refunds.** The same page has two more buttons: **Refund** gives back the last rank (what you paid for it) and
**Refund all** (click twice to confirm) removes every rank and gives back all the Pure Slorm you spent.
The price paid for each rank is recorded, so a later change of prices never costs you anything.
Going down a rank gives the room back, from the next floor on.

**During a floor**, a line under the timer tells you what the upgrade did, for as long as you play it:
"-2 rooms: 4 instead of 6", "... (minimum reached)" or "No room removed: already at the minimum".

The **Language** selector at the top of the page changes its language (it is remembered; the first
time it follows the language of Windows). Esc, the X button or a click outside the page closes it.

The level is saved **per save slot** in `TempleRush_rooms.txt`, in the game folder (not in the game's own
save file). Copy that file along with your save if you move to another PC.

## Timer on screen

During a Temple floor, a frame like the title of an item appears at the top right of the screen, under the game's
"Find the Shrine" line and its bar of Slorm to spend. It reads **NEXT FLOOR +N** and the time left to get that
bonus. The border and the text change color with the bonus, like the rarity of an item: blue-grey for +2, gold
for +3, orange for +4 and purple (Nether) for +5. The countdown starts when the first monsters of the floor appear:
until then the frame shows the full time, frozen.

The monsters appear room by room, so the countdown keeps running between rooms. In the **last room** (the
one with the altar), as soon as the last monster is dead the countdown **freezes**: "OK" is added and the bonus you
reached is kept, so walking to the altar and picking up the Slorm costs you nothing. If monsters show up again, it goes on.

| Frame | Meaning |
|-------|---------|
| `NEXT FLOOR +5  0:23` (purple) | 23 seconds left to clear the floor for +5 floors |
| `NEXT FLOOR +2  1:12` (blue-grey) | 1 min 12 s left to clear the floor for +2 floors |
| `NEXT FLOOR +0  0:00` (grey) | too late, no bonus (disappears after 5 seconds) |
| `NEXT FLOOR +4  0:05 OK` (orange) | last room cleared: the countdown is frozen and the bonus is kept |
| `NEXT FLOOR +3` | after the clear: the bonus you got (shown for 6 seconds) |

It only shows in the Slorm Temple, not in the Battlefield or the Great Forge.

**Boss floors.** The game has a boss every 50 floors (50, 100 and 150) and starts a run on your highest floor. A
bonus never carries you past a boss floor: it stops one floor before, so you can play the boss and get its artifact.

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

### 1.3.1
- The switch is now a gold-framed **Temple rush** box (the game's own frame and font) on the NPC's portrait, centered on it.
- The page is more compact: the Slorm you own and the description of the upgrade sit on a dark red background, and the Pure Slorm icon follows the price and the refunds.
- The timer is now a frame like the title of an item, with "NEXT FLOOR +N" and the time. The border and the text change color with the bonus: blue-grey +2, gold +3, orange +4, purple +5. It sits under the game's bar of Slorm to spend.
- The message about the rooms removed stays on screen during the floor (it used to disappear after 8 seconds).
- The time bonus no longer carries you past a boss floor (50, 100, 150): you land on the boss floor, so you can play it.

### 1.3.0
- New page for the upgrade, in the style of SlormReslormitizer's panel (a mod by wistb): the game's own frame, selector, buttons and fonts. The switch is now its grey (blue when open) Slormite star on the NPC's portrait, instead of the M diamond.
- "Temple Floor Room Count" now has 10 ranks of 1 room each (up to -10 rooms) with new prices (210,297 Pure Slorm in total), instead of 50 levels of 0.1 room.
- Nothing you invested is lost: the price paid for each rank is recorded and refunded as paid, and **Refund all** works even at rank 0. Old levels are converted (the same whole rooms stay removed), and the levels that do not make a whole rank are refunded automatically, once.
- The portal's destination after a record with a bonus was already fixed in 1.2.4.

### 1.2.4
- Fixed the portal: after a new record with a time bonus, it now takes you to the floor you cleared + 1 + the bonus (it used to send you to the next floor only, especially on the first floor after starting the game).

### 1.2.3
- The "M" icon that opens the upgrade page moved to the bottom left of the window, next to the "?" button.

### 1.2.2
- The timer moved under the game's "Find the Shrine" line (top right) and scales with the resolution.
- The countdown freezes in the last room (where the altar is) once the last monster is dead, and the bonus is kept.

### 1.2.1
- Refund buttons on the upgrade page: **Refund** (last level) and **Refund all** (asks for a second click).
- On entering a floor, a box tells how many rooms the floor has and how many it would have had without the upgrade.
- Upgrade price lowered to 50 x level (it was 500 x level in 1.2.0).

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
