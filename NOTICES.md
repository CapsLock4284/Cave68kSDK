# Notices

Cave68kSDK contains code written after, or taken from, the following projects. All are under the
BSD-3-Clause licence.

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

## tmntarcade-decomp

The runtime's structure and its .zip reading (`src/rom_set.c`, `src/inflate.c`) come from
tmntarcade-decomp's runtime, BSD-3-Clause, copyright (c) 2026 Sebastian Toyos. Its licence is in
`LICENSE.tmnt-runtime`.
