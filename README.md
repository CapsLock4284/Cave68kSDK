# Cave68kSDK

A runtime for recompiled games on Cave's 68000 arcade boards (1994–1997: DonPachi, DoDonPachi, ESP Ra.De.,
Guwange, Dangun Feveron and the rest of MAME's `cave.cpp`). It is the board half of a static recompilation:
the game's 68000 program is translated to C ahead of time, and this library is everything that program talks
to — the bus, the video chips, the sound chips, the EEPROM, the inputs and the ROM set.

It began as the runtime of [DonPachi Recomp](https://github.com/CapsLock4284/DonPachi-Recomp), split out so
that more games can share it. DonPachi is the first game it describes and the one it is tested against.

Only the Cave 68000 boards are in scope. Other hardware (Cave's later SH-3 boards, other makers' 68000
boards) is not planned.

## How a game is described

The boards differ from game to game: where each device sits on the bus, how many tile layers there are,
the sprite format, the sound chips and the ROM files. Each game gets a **board description**
(`include/cave/board.h`), which is data only:

| Part | Type | What it says |
|---|---|---|
| Bus | `cave_region[]` | address ranges and the device behind each (program ROM, work RAM, a layer's RAM, palette, M6295, NMK112, inputs, EEPROM, …), looked through in order |
| ROM set | `cave_rom_desc` | the versions (MAME's sets, parent and clones), their files by name, CRC32 and size, and where each file is loaded (as is, byte-swapped, or interleaved into even and odd bytes) |
| Picture | `cave_video_desc` | the number of layers, which are 8×8 only, the layer offsets, colour bases, sprite format and sprite bank size |
| Sound | `cave_sound_desc` | the chip kind, clocks, divisor, mix levels and NMK112 layout; which sounds are music, effects and voices (each chip's group, and the sounds that differ), for the ports' separate volumes (`cave_sound_set_level`, cave/sound.h) |
| Credits | `cave_credits_desc` | where the program counts its credits and the setting that makes a game take 2, for free play (`free_play.c`: Start drops in the coins first) |

The descriptions live in `src/boards/`, one file per game, and are listed in `include/cave/boards.h`.
A game chooses its board before its reset:

```c
#include "cave/boards.h"

cave_board_describe(&cave_board_donpachi);
```

Until a game chooses, the board is DonPachi's.

## What is in it

| File | What it does |
|---|---|
| `src/machine.c` | the board: the 68000 bus (driven by the description), interrupts, EEPROM (93C46), inputs, the state hash |
| `src/video.c` | the NEC 038 tile layers, the 013 sprites and the palette, drawn to a 320×240 frame; or as planes (each layer and the sprites apart, keyed by priority) for a port to scale one by one and put together again (`video_draw_planes`, `video_compose`) |
| `src/sound.c` | two OKI M6295s behind an NMK112, or a YMZ280B (its voices, and its interrupt to the 68000 at the sample a voice ends), resampled and mixed as MAME does, 48 kHz |
| `src/rom_set.c` | the ROM set from a .zip or folder (files known by CRC32, merged and split sets, clones over their parent), and the extracted data file |
| `src/game_program.c` | the glue between the recompiled program and the board: the frame loop, save states, the program's tasks |
| `src/tasks.c`, `include/host/tasks.h` | the recompiled program's cooperative tasks |
| `src/cheats.c`, `src/free_play.c` | MAME-format cheats; free play (Start drops in the coins a game needs, then presses Start until the game takes it) |
| `src/inflate.c` | Deflate for .zip reading |
| `src/boards/donpachi.c` | DonPachi's description (US and Japanese sets) |
| `src/boards/ddonpach.c` | DoDonPachi's description (World and Japanese sets): bus, ROM set, picture and sound |
| `port/` | the port kit: the SDL2 parts every game's port shares (below) |

## The port kit (`port/`)

The parts of a game's SDL2 port that are the same for every game, kept here so each is written once. They are
not in the `cave68k` library (they need SDL2): a game compiles them into its own program, with `port/` on its
include path and its names given to `paths.c`:

| File | What it does |
|---|---|
| `port/paths.c` | where the player's files are kept: beside the program, or the user's own folder for the game when that can't be written (`CAVE_PORT_FOLDER`, e.g. `"DonPachi"`; `CAVE_PORT_STEM`, e.g. `"donpachi"`) |
| `port/file_save.c` | the player's files saved by a helper thread, so a frame never waits for the disk |
| `port/updater.c` | the program updating itself from the game's GitHub releases (WinHTTP on Windows, the curl command elsewhere) |
| `port/scaler.c` | the pixel-art scalers: Scale2x, Scale3x, xBR 2x/3x/4x, MMPX |
| `port/layer_filter.c` | a scaler run on each layer and the sprites apart, then put together again (`video_draw_planes`) |
| `port/crt.c` | the CRT look: scanlines, the mask, the glow, the curve |
| `port/gadget_canvas.c`, `port/gadget_font.h` | drawing for the gadget side panels: boxes, gradients and smooth text at any size, in Chakra Petch (`port/fonts/OFL-ChakraPetch.txt`; `tools/make_gadget_font.py` writes the font's header) |

```cmake
set(CAVE_PORT_KIT runtime/port/paths.c runtime/port/updater.c runtime/port/crt.c runtime/port/scaler.c
	runtime/port/layer_filter.c runtime/port/file_save.c runtime/port/gadget_canvas.c)
add_executable(mygame WIN32 ports/sdl2/main.c ... ${CAVE_PORT_KIT})
target_include_directories(mygame PRIVATE runtime/port)
target_compile_definitions(mygame PRIVATE CAVE_PORT_FOLDER="MyGame" CAVE_PORT_STEM="mygame")
```

## Building

The library is plain C99 with no dependencies.

```sh
cmake -S . -B build
cmake --build build
```

This builds the static library `cave68k`. A game links it together with its own recompiled program, which
provides the program's code and tables that `game_program.c` calls (DonPachi's are generated by its
recompiler into `src/` and `src_jpn/`). The SDK can also be added to a game's own CMake build:

```cmake
add_subdirectory(Cave68kSDK)
target_link_libraries(mygame PRIVATE cave68k)
```

## Verification

Every change is checked against DonPachi by tracing the board for 30,000 frames of play (a hash of the
board's memories and of the sound every 500 frames, and of the drawn picture every 100), for the US and the
Japanese versions, and comparing with the traces of the runtime before the change. The extracted data
files must also stay byte for byte the same, so that players' existing files keep working. The tools for
this are in DonPachi Recomp's `tests/harness`.

## Roadmap

1. **Done:** the runtime split out of DonPachi Recomp, with its history.
2. **Done:** the board description: bus, ROM set, picture and sound settings taken out of the code and into
   `src/boards/donpachi.c`, with DonPachi unchanged (identical traces).
3. **Done:** DonPachi Recomp builds against this SDK as its `runtime/` submodule.
4. **Started:** a second game, DoDonPachi. Its program, translated by DoDonPachi Recomp's tools, runs on the SDK:
   its warning, title and attract demo are drawn right, and over 72,000 VBLANKs of play it matches the original
   program on MAME's 68000 core at every one of 70,684 game frames: the same board, and the same frames slowed
   down. Done: its bus map and ROM set, its 8-bit text layer, its palette's banks of 256, its sprites, the interrupt
   cause's bit 2 as MAME has it, the board's memory addresses per game (`game_board.h`). To do: the YMZ280B sound
   chip.
5. More of the family as they are wanted: the 013's zooming sprite format, row-scroll and row-select on the
   layers, flipped screens, other screen sizes (384×240 boards) and four-layer boards.

## Licence

BSD-3-Clause: see `LICENSE`. The board code follows MAME's drivers and devices (BSD-3-Clause; each file
names its sources), and the .zip reading comes from tmntarcade-decomp's runtime (BSD-3-Clause,
`LICENSE.tmnt-runtime`). See `NOTICES.md`.

No ROMs, and nothing extracted from them, are part of this repository or may be added to it. Players
supply their own ROM sets.
