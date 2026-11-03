# Black Cauldron Enhanced

Version 0.2.3

Play **The Black Cauldron** (Sierra On-Line / Disney, 1986-87) on modern
Windows without DOSBox. You need your own copy of the original game files.

The original game runs on Sierra's AGI engine (Adventure Game Interpreter,
version 2.439 for this game). Black Cauldron Enhanced is a new AGI
interpreter written from scratch in C, with SDL3 for the window, sound and
gamepads. It reads the original `LOGDIR`, `PICDIR`, `VIEWDIR`, `SNDDIR`,
`OBJECT`, `WORDS.TOK` and `VOL.*` files and runs the game's own scripts. The
game logic is unchanged; the interpreter adds modern controls on top.

## What works

- The whole interpreter: logic scripts, pictures, animated objects, motion,
  priority and control lines, inventory, menus, message windows.
- **Display.** Window or full screen, 4:3 like a 1980s monitor, and the CRT
  shaders of DOSBox Staging. By default the picture looks like a 1986 EGA
  monitor (`monitor = ega`): one picture line per scanline, as an EGA monitor
  drew it, with DOSBox Staging's EGA presets and EGA colours (P22 phosphors,
  9300 K white point, slightly raised black level). `monitor = vga` gives the
  double-scanned look of a 1990s VGA monitor instead. `shader = crt-auto`
  picks the preset from the height of the picture, as DOSBox Staging does:

  | Picture height | EGA preset |
  |---|---|
  | 1600 pixels and more | `ega-4k` |
  | 1000 to 1599 | `ega-1440p` |
  | 800 to 999 | `ega-1080p` |
  | 600 to 799 | `ega-720p` |
  | less | no CRT effect |

  Ctrl+F6 switches the shader on and off, Ctrl+F5 switches between EGA and
  VGA monitor.
- **Sound: PCjr / Tandy or PC speaker.** The PC version of the game contains
  music for the three tone voices and the noise voice of the PCjr and Tandy
  1000 (SN76496 chip). With `sound = tandy` (the default) you hear all four
  voices. `sound = pcspeaker` plays only the first voice as a square wave, as
  the PC speaker of most PCs did in 1986. Ctrl+F4 switches between the two.
  + and - set the volume in steps of 10 % (saved as `volume` in the ini
  file). The original game allowed this only on a Tandy 1000; the help page
  now always mentions it.
- **Mouse.** Left click: Taran walks to the pointer. A click near the left,
  right or bottom edge, in the black area below the picture, or above the
  horizon makes Taran walk on until he touches that edge, so you leave the
  room there, just as with the arrow keys. Right click: an action
  menu in the style of the game with *Select*, *Use*, *Do* and *Look*.
  When the pointer is over the status bar at the top, it shows the menu bar;
  a click there opens that menu, as in the Amiga and Atari ST versions. In
  an open menu the highlight follows the pointer. The pointer is a blocky
  arrow on the 160-pixel grid of the AGI games, drawn in the game picture so
  it gets the same CRT look. `pointer = agi-triangle` is the same arrow head
  without the stem, `pointer = sci` the finer arrow of the later Sierra
  games and `pointer = windows` the normal pointer. When
  Taran cannot walk (intro, cut scenes), a click counts as a key press. For *Do*
  and *Use*, Taran first walks to the place you clicked and then does the
  action (configurable with `walk_first`). *Use* shows the object you chose
  with *Select*. The mouse also works in menus, the inventory and message
  windows (left = OK, right = cancel).
- **Gamepad.** See the table below.
- **IBM PC font.** Text uses the 8x8 font of the IBM PC ROM, as the
  original did on an EGA card.
- **Help (F1).** The original two help pages: keyboard (white) and, instead
  of the joystick page, a page about the mouse and the controller (black).
- **Save and restore.** F5 / F7, 12 slots, files `bce-save-NN.sav` in the
  game folder. Restore also lists the saved games of the original DOS
  version (`BCSG.1` to `BCSG.12` in the game folder) and loads them: the
  picture is rebuilt from the original's script buffer, so you continue
  exactly where you were.

## Installation

1. Copy `BC-Enhanced.exe` into the folder with the game files (or into a
   folder directly below it).
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
| Ctrl+F4 | PCjr/Tandy sound / PC speaker (saved in the ini file) |

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
- IBM PC 8x8 font (`src/font_ibm8x8.inc`): taken from DOSBox Staging
  (`src/ints/int10_memory.cpp`), GPL-2.0-or-later, (c) The DOSBox Team and
  The DOSBox Staging Team.
- Icon (`src/bce.ico`, `art/icon32.png`): original pixel art made for this
  project, GPL-3.0-or-later like the rest.
- The Black Cauldron is (c) Sierra On-Line and The Walt Disney Company. No game
  files are included.
