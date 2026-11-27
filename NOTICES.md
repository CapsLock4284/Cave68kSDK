# Notices

Cave68kSDK contains code written after, or taken from, the following projects. All are under the
BSD-3-Clause licence, but for the port kit's sources named below.

## MAME

https://github.com/mamedev/mame — BSD-3-Clause, copyright the MAME contributors. The board, video and
sound code follow these files (each source file names the parts it follows and their authors):

- the Cave driver, `cave.cpp` and `cave_v.cpp` (Luca Elia and others): the memory maps, the ROM sets, the
  interrupts, the EEPROM wiring, the screen
- `sprite013.cpp`: the 013 sprites
- `src/devices/video/tmap038.cpp`: the 038 tile layers
- `src/devices/sound/okim6295.cpp` (Mirko Buffoni, Aaron Giles), `okiadpcm.cpp` (Andrew Gardner, Aaron
  Giles): the M6295 and its ADPCM
- `src/devices/machine/nmk112.cpp` (Alex W. Jackson): the NMK112
- `src/devices/sound/ymz280b.cpp` (Aaron Giles): the YMZ280B
- `src/emu/sound.cpp`, `resampler.cpp`, `audio_effects/filter.cpp`: the resampler and the output filter

## The port kit (`port/`)

- `port/scaler.c`: this project's own code, after the published algorithms: Scale2x/Scale3x (EPX, as
  AdvanceMAME describes it), xBR (Hyllian, whose shaders are under the MIT licence) and MMPX (Morgan McGuire and
  Mara Gagiu, 2021, MIT licence).
- `port/gadget_canvas.c`: the glyphs' filling after font-rs (Raph Levien, Apache-2.0/MIT: its accumulation
  rasteriser, written again in C).
- `port/gadget_font.h`: the outlines of [Chakra Petch](https://github.com/cadsondemak/Chakra-Petch), Copyright 2018
  The Chakra Petch Project Authors, under the SIL Open Font License 1.1 (`port/fonts/OFL-ChakraPetch.txt`). A game
  that uses it carries that licence with its program.
- The port kit uses [SDL2](https://www.libsdl.org/) (zlib licence), which a game supplies.

## tmntarcade-decomp

The runtime's structure and its .zip reading (`src/rom_set.c`, `src/inflate.c`) come from
tmntarcade-decomp's runtime, BSD-3-Clause, copyright (c) 2026 Sebastian Toyos. Its licence is in
`LICENSE.tmnt-runtime`.
