<img width="1240" height="754" alt="image" src="https://github.com/user-attachments/assets/0219571e-b454-4953-9dd2-4ce269b93e57" />

# Black Cauldron Enhanced

(Version 1.35)

Black Cauldron Enhanced enables you to play **The Black Cauldron** (Sierra On-Line / Disney, 1986-87) with several QoL-fixes in Windows, without the need for DosBox. This enhanced version is an AGI interpreter, that uses SDL3 for the graphics, sounds and controls. It started out as an idea to add mouse controls to the game, where you are able to point and left-click to walk  to a location, while a right-click brings up a menu with the available actions. It didn't take long to add all sorts of small QoL-fixes to make the game more enjoyable. The current version has the following improvements:

Gameplay
- The ability to play the game on Windows, without the use of DosBox. You'll need the game's original files, either from your own disks or Al Lowe's website.
- The ability to rewind to an earlier scene or room, in addition to the normal restore, restart and quit options of the original. The game 'records' up to 5 scenes.
- The ability to disable hunger and thirst, preventing Taran from dying if you do not constantly look after his well-being.

Graphics
- The ability to use an EGA or VGA-shader, or no shader at all. The EGA shader lets you experience the game as it was played back in the day (Though I only had a CGA monitor!)
- An AGI-style mouse pointer, instead of the basic Windows-cursor. You can also opt for a pointer that resembles the later SCI-style arrow.
- Text is displayed in the traditional IBM 8x8-pixel font.

Sound
- Emulation of both the Tandy/PCjr-sound and the PC-speaker, that was available in the original game.
- An AdLib-interpretation of how  the music could have sounded on this slightly more modern soundcard, that was introduced in 1987.

Controls
- The ability to use the mouse or a modern controller to move Taran and execute commands, instead of function keys.




## Where to get the game

Al Lowe offers The Black Cauldron as a free download on his website: <https://allowe.com/downloads/games.html> (the file `The Black Cauldron.zip`). He allows people to copy and share it, but he gives no support for it. 

## Installation

1. Copy `BC-Enhanced.exe` into the folder with the game files (or into a folder directly below it). For the download from Al Lowe: unzip `The Black Cauldron.zip` and put the exe in the folder with `LOGDIR`.
   
3. Start `BC-Enhanced.exe`.

On the first start, the program writes `bc-enhanced.ini` next to the exe, with an explanation for every setting.

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

Additional keys added by Black Cauldron Enhanced are:

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

`pointer_stick = left` swaps the sticks: the left stick moves the pointer and the right stick walks. `rightstick_speed` sets the speed of the pointer. Every button can be changed in the `[gamepad]` section of the ini file.

## Building from source

The Windows build is cross-compiled on Linux with clang, lld, mingw-w64 (built from source) and a static SDL3 3.2. See `build.sh`. The engine itself
(`src/res.c`, `objects.c`, `text.c`, `logic.c`, `engine.c`) has no SDL dependency; `tools/headless.c` runs the game without a window from a script (see `tests/`) and saves screenshots. `tools/listing.py` disassembles the game's logic scripts (`BC_GAMEDIR=<folder> python3 tools/listing.py 0`).

## Licences

- Black Cauldron Enhanced source code: GPL-3.0-or-later, (c) 2026 Zwarteziel. See `LICENSE`.
- CRT shaders and image adjustments (`third_party/dosbox-staging-shaders`): MIT, (c) The DOSBox Staging Team, Hyllian, Tyrells and others. See the README in that folder.
- SDL3 (linked statically in the Windows build): zlib licence.
- Nuked-OPL3 1.8 (`third_party/nuked-opl3`), the emulation of the AdLib's Yamaha OPL chip: LGPL-2.1-or-later, (c) Nuke.YKT. See the LICENSE in that folder.
- IBM PC 8x8 font (`src/font_ibm8x8.inc`): taken from DOSBox Staging (`src/ints/int10_memory.cpp`), GPL-2.0-or-later, (c) The DOSBox Team and The DOSBox Staging Team.
- Icon (`src/bce.ico`, `art/icon32.png`): original pixel art made for this project, GPL-3.0-or-later like the rest.
- The Black Cauldron is (c) Sierra On-Line and The Walt Disney Company. No game
  files are included.
