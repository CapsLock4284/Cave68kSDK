# Changelog

What has changed in Cave68kSDK, newest first, by date. The SDK has no releases of its own: each game takes a commit
of it as its `runtime/` submodule.

## 2026-11-27

### Added
- **ESP Ra.De.'s board** (`src/boards/esprade.c`, `cave_board_esprade`): DoDonPachi's bus with three whole 8-bit
  layers, the YMZ280B, the VBLANK's interrupt 2,000 µs early; its three versions' ROM sets.
- **The 013's zooming sprites** (`CAVE_SPRITES_013_ZOOM`): sprite013.cpp's zooming format and drawing (each pixel
  at most once, as MAME), and sprites at 8 bits a pixel (`sprite_bits`, MAME's layout_sprites_8bpp).
- `CAVE_LOAD_LONG_SWAPPED`: ROM_LOAD32_WORD_SWAP.
- `m68k_passed` (game/support.h): a value a routine pushes for the routines it then calls, which read it from its
  stack (ESP Ra.De.'s 03AF1C), handed over by the translated C.
- `m68k_saved` (game/support.h): words a routine pushes in a loop and pops back after a call, kept by the runtime for
  the translated C (ESP Ra.De.'s 03B7D8).
- `cave_write_seen` (cave/machine.h): a probe hook for the tools, each write to the board's devices (not the work
  RAM) with its address, data and mask.

### Changed
- `video.c`: the no-zoom sprite drawing as a function of its own (DonPachi's pictures the same).

### Fixed
- The interrupt's cause read at the very cycle the VBLANK comes: the VBLANK counts as begun for that read
  (`cave_vblank_due`, set by game_program.c in a timed program), as MAME's 68000 sees it. ESP Ra.De. polled it there
  in its 5th game frame and counted its random index one round off.

## 2026-11-26

### Added
- **The port kit** (`port/`): the SDL2 parts every game's port shares, as DonPachi Recomp and DoDonPachi Recomp had
  them, now in one place: where the player's files are kept (`paths.c`, the game's names from its build:
  `CAVE_PORT_FOLDER`, `CAVE_PORT_STEM`), background saving, the updater, the pixel-art scalers, the per-layer
  filter, the CRT look, and the gadget panels' drawing with its font (Chakra Petch, SIL OFL 1.1) and
  `tools/make_gadget_font.py`. Built into each game as the same machine code as before.
- `cave_irq_cause_read`: a hook for the test tools on each read of the interrupt's cause and the OKI M6295s' status,
  with the value read (the games' checks against the real MAME).

## 2026-11-25

### Changed
- **The VBLANK's interrupt 90 µs before the frame's end**, as MAME's `cave.cpp` has it (`vblank_lead_usec` in the
  board description; `program_set_vblank_lead`, off for older replays).
- `program_at_screen_vblank`: a call at the screen's own VBLANK, the lead after the interrupt, where MAME draws the
  picture.

## 2026-11-20

### Added
- The picture in planes (`video_draw_planes`, `video_compose`): each layer and the sprites apart, for a port to scale
  them one by one.

## 2026-11-19

### Added
- Sound groups: music, effects and voices with their own levels (`cave_sound_set_level`), for OKI boards and the
  YMZ280B; DonPachi's and DoDonPachi's.

## 2026-11-17

### Added
- Free play for every game: each board description says where its credits are.
- The look-ahead picture's support: `cave_sound_quiet`; save states that keep the program's timing and the
  EEPROM's transfer.

### Changed
- The picture 3.7 times faster (the layers in one pass, the sprites clipped once), the same pictures.

## 2026-11-15 to 2026-11-16

### Added
- **DoDonPachi**: its board description (bus, ROM sets World and Japan, byte-interleaved loads, a 64 KB palette in
  banks of 256), its 8-bit text layer, word-swapped sprites, the interrupt cause's bit 2, the layers' row-scroll and
  row-select, and the YMZ280B (its voices, mix and interrupt at the sample a voice ends).
- Traced builds can log their blocks (`game_trace_file`), for frame-by-frame checks against the original.
- **Board descriptions**: the memory map, ROM set, picture and sound as data (`include/cave/board.h`), DonPachi
  first; the README, CMake build, licence and notices.

## Before 2026-11-15

The runtime as part of DonPachi Recomp (its history is kept here): the board, its video and sound (two OKI M6295s
and an NMK112), the EEPROM, the ROM set reader, the arcade's slowdown counted instruction by instruction, save
states, cheats and the Japanese version.
