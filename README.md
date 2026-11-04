# Black Cauldron Enhanced

Version 1.34

Play **The Black Cauldron** (Sierra On-Line / Disney, 1986-87) on modern
Windows without DOSBox. You need the original game files: your own copy, or
the free download from Al Lowe, the game's designer (see *Where to get the
game* below).

The original game runs on Sierra's AGI engine (v2.439 for this game). 
Black Cauldron Enhanced is a new AGI interpreter written from scratch in C,
with SDL3 for the window, sound and gamepads. It reads the game's original
files and runs the game's own scripts. The game logic is unchanged. 

Black Cauldron Enhanced features:

- **Modern controls**: you can use a mouse or controller to move Taran and
  execute commands with an action menu, instead of function keys.
- A **CRT-shader** that emulates the way the game looked on an EGA monitor.
  you can also choose a VGA-monitor shader or turn it off completely.
- An **IBM PC font**, that shows texts the way it looked on an EGA monitor.
- A custom **mouse pointer**, that can be configured.
- Emualation of **Tandy and PC-speaker** sound.
- A new **AdLib interpretation** of the music and sound effects, used by
  default. The game never had AdLib music; this plays the Tandy notes on an
  emulated AdLib card (FM synthesis). Choose flute (the default), organ,
  harp, or flute-harp (the melody on a flute, the accompaniment on a harp)
  with `adlib_instruments` in the ini file.
- **Hunger and thirst** can be switched off in the Special menu (or with
  `hunger_thirst=off` in the ini file), so Taran never starves or dies of
  thirst. They are on by default, as in the original game.
- **Rewind** to the moment you entered one of the last five scenes: after a
  fatal mistake the game offers it next to the original Restore, Restart
  and Quit, and File > Rewind (Ctrl+Z) does it at any time. A picture of each scene helps you choose. The last scene entry is
  also kept in `bce-autosave.sav`, which Restore lists as well.

## Where to get the game

Al Lowe offers The Black Cauldron as a free download on his website:
<https://allowe.com/downloads/games.html> (the file
`The Black Cauldron.zip`). He allows people to copy and share it, but he
gives no support for it. 

## Installation

1. Copy `BC-Enhanced.exe` into the folder with the game files (or into a
   folder directly below it). For the download from Al Lowe: unzip
   `The Black Cauldron.zip` and put the exe in the folder with `LOGDIR`.
2. Start `BC-Enhanced.exe`.

On first start the program writes `bc-enhanced.ini` next to the exe, with an
explanation for every setting.

## Controls

The original keys all work:

| Key | Action |
|---|---|
| Arrow keys / keypad | Walk (press again to stop; `arrow_keys=hold` walks while held) |
| F3 | Select an object ("Select Object"; the original menu said "New Object") |
| F4 | Use the selected object |
| F6 | Do (pick up, open, talk...) |
| F8 | Look |
| Tab | Inventory |
| Esc | Menu bar |
| F1 / F2 | Help / sound on-off |
| F5 / F7 / F9 | Save / restore / restart |
| Ctrl+Z | Rewind to an earlier scene (added by Black Cauldron Enhanced) |
| F10 | Change speed |
| + / - | Volume louder / softer |
| Alt+Z | Quit |

Added by Black Cauldron Enhanced:

| Key | Action |
|---|---|
| Alt+Enter or F11 | Full screen on/off |
| F12 | Screenshot (`bceNNNN.bmp`) |
| Ctrl+F6 | CRT shader on/off (saved in the ini file) |
| Ctrl+F5 | EGA / VGA monitor (saved in the ini file) |
| Ctrl+F4 | PCjr/Tandy sound / AdLib / PC speaker (saved in the ini file) |

## Default gamepad layout

| Gamepad | Game |
|---|---|
| Left stick / d-pad | Walk (8 directions, while held); in menus: navigate |
| Right stick | Move the mouse pointer |
| A | Left click at the pointer: Taran walks there (in menus and windows: OK) |
| X | Do |
| Y | Select an object |
| B | Use the selected object (in menus and windows: back) |
| RB | Look |
| LB / Start | Menu bar (Esc) |
| Back / View | Inventory |

`pointer_stick = left` swaps the sticks: the left stick moves the pointer
and the right stick walks. `rightstick_speed` sets the speed of the pointer.
Every button can be changed in the `[gamepad]` section of the ini file.

## Building from source

The Windows build is cross-compiled on Linux with clang, lld, mingw-w64 (built
from source) and a static SDL3 3.2. See `build.sh`. The engine itself
(`src/res.c`, `objects.c`, `text.c`, `logic.c`, `engine.c`) has no SDL
dependency; `tools/headless.c` runs the game without a window from a script
(see `tests/`) and saves screenshots. `tools/listing.py` disassembles the
game's logic scripts (`BC_GAMEDIR=<folder> python3 tools/listing.py 0`).

## Licences

- Black Cauldron Enhanced source code: GPL-3.0-or-later, (c) 2026 Zwarteziel.
  See `LICENSE`.
- CRT shaders and image adjustments (`third_party/dosbox-staging-shaders`): MIT,
  (c) The DOSBox Staging Team, Hyllian, Tyrells and others. See the README in
  that folder.
- SDL3 (linked statically in the Windows build): zlib licence.
- Nuked-OPL3 1.8 (`third_party/nuked-opl3`), the emulation of the AdLib's
  Yamaha OPL chip: LGPL-2.1-or-later, (c) Nuke.YKT. See the LICENSE in that
  folder.
- IBM PC 8x8 font (`src/font_ibm8x8.inc`): taken from DOSBox Staging
  (`src/ints/int10_memory.cpp`), GPL-2.0-or-later, (c) The DOSBox Team and
  The DOSBox Staging Team.
- Icon (`src/bce.ico`, `art/icon32.png`): original pixel art made for this
  project, GPL-3.0-or-later like the rest.
- The Black Cauldron is (c) Sierra On-Line and The Walt Disney Company. No game
  files are included.
