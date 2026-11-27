/**
 *    Name:        esprade.c
 *    Description: ESP Ra.De.'s board described (cave/board.h): its bus, ROM set, picture and sound
 *    Version:     0.1.0 (draft)
 *    Created:     26-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/atlus/cave.cpp (BSD-3-Clause, Luca Elia and others): esprade_map, ROM_START( esprade ),
 *    ROM_START( espradej ) and ROM_START( espradejo ), the esprade machine and init_esprade.
 *      000000-0FFFFF  program ROM (two chips, u42 the words' high bytes)     100000-10FFFF  work RAM
 *      300000-300003  YMZ280B           400000-40FFFF  sprite RAM
 *      500000-507FFF  layer 0 (038)     600000-607FFF  layer 1 (038)     700000-707FFF  layer 2 (038)
 *      800000-80007F  video registers (800000-800007 read: the interrupt's cause)
 *      900000/A00000/B00000  layers 0, 1, 2: their 3 registers     C00000-C0FFFF  palette
 *      D00000  inputs 1     D00002  inputs 2 + the EEPROM's data out (bit 11)     E00000  EEPROM lines
 *    DoDonPachi's bus, but for layer 2: a whole layer, not 8x8 text. The program's chips checked by its reset vector:
 *    u42 as the high bytes gives the stack at 110000 and the start at 00595A (the Japanese sets' 00594E).
 *    The picture (MAME's): all three layers 8 bits a pixel (gfx_esprade: layout_8x8x8, colours from 4000, 64 banks
 *    of 256); the sprites the 013's zooming format (set_alt_format(false)), 8 bits a pixel (layout_sprites_8bpp),
 *    granularity 256; their ROMs ROM_LOAD32_WORD_SWAP (u63 and u64 the first 8 MB, u65 and u66 the second).
 *    The sound: MAME's add_ymz (a YMZ280B at 16.9344 MHz, mono at 1.0). The VBLANK's interrupt 2,000 us before the
 *    frame's end (init_esprade: m_time_vblank_irq = 2000).
 *    Not checked yet against the pictures and the sound: all of it (the program does not run yet).
 */
#include "cave/boards.h"

#include <stddef.h>

static const cave_region esprade_map[] = {
	{ 0x000000u, 0x100000u, CAVE_REGION_PROGRAM, 0, 0, 0 },
	{ 0x100000u, 0x110000u, CAVE_REGION_WORK_RAM, 0, 0, 0 },
	{ 0x300000u, 0x300004u, CAVE_REGION_YMZ280B, 0, 0, 0 },
	{ 0x400000u, 0x410000u, CAVE_REGION_SPRITE_RAM, 0, 0, 0 },
	{ 0x500000u, 0x508000u, CAVE_REGION_LAYER_RAM, 0, 0, 0 },
	{ 0x600000u, 0x608000u, CAVE_REGION_LAYER_RAM, 1, 0, 0 },
	{ 0x700000u, 0x708000u, CAVE_REGION_LAYER_RAM, 2, 0, 0 },
	{ 0x800000u, 0x800080u, CAVE_REGION_VIDEO_REGS, 0, 0, 0 },
	{ 0x900000u, 0x900006u, CAVE_REGION_LAYER_REGS, 0, 0, 0 },
	{ 0xA00000u, 0xA00006u, CAVE_REGION_LAYER_REGS, 1, 0, 0 },
	{ 0xB00000u, 0xB00006u, CAVE_REGION_LAYER_REGS, 2, 0, 0 },
	{ 0xC00000u, 0xC10000u, CAVE_REGION_PALETTE, 0, 0, 0 },
	{ 0xD00000u, 0xD00002u, CAVE_REGION_INPUTS, 0, 0, 0 },
	{ 0xD00002u, 0xD00004u, CAVE_REGION_INPUTS, 1, 0, 0 },
	{ 0xE00000u, 0xE00002u, CAVE_REGION_EEPROM, 0, 0, 0 },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* the files, in the same order in every version: each Japanese set's own program, the rest the World set's */
enum { U42, U41, U63, U64, U65, U66, U54, U55, U52, U53, U51, U19, EEPROM };

#define COMMON_FILES \
	{ "esp_u63.u63", 0x2f2fe92cu, 0x400000u }, \
	{ "esp_u64.u64", 0x491a3da4u, 0x400000u }, \
	{ "esp_u65.u65", 0x06563efeu, 0x400000u }, \
	{ "esp_u66.u66", 0x7bbe4cfcu, 0x400000u }, \
	{ "esp_u54.u54", 0xe7ca6936u, 0x400000u }, \
	{ "esp_u55.u55", 0xf53bd94fu, 0x400000u }, \
	{ "esp_u52.u52", 0xe7abe7b4u, 0x400000u }, \
	{ "esp_u53.u53", 0x51a0f391u, 0x400000u }, \
	{ "esp_u51.u51", 0x0b9b875cu, 0x400000u }, \
	{ "esp_u19.u19", 0xf54b1cabu, 0x400000u }, \
	{ "eeprom-esprade.bin", 0x315fb546u, 0x000080u }

static const cave_rom_file world_files[] = {
	{ "u42.int", 0x3b510a73u, 0x080000u },
	{ "u41.int", 0x97c1b649u, 0x080000u },
	COMMON_FILES,
};

static const cave_rom_file japan_files[] = {
	{ "u42_ver.2", 0x75d03c42u, 0x080000u },
	{ "u41_ver.2", 0x734b3ef0u, 0x080000u },
	COMMON_FILES,
};

static const cave_rom_file japan_old_files[] = {
	{ "u42.bin", 0x0718c7e5u, 0x080000u },
	{ "u41.bin", 0xdef30539u, 0x080000u },
	COMMON_FILES,
};

static const cave_rom_version esprade_versions[] = {
	{ "esprade", "WORLD", -1, world_files },           /* World, 1998 4/22 International Ver. */
	{ "espradej", "JAPAN", 0, japan_files },           /* Japan, 1998 4/21 Master Ver. */
	{ "espradejo", "JAPAN OLD", 0, japan_old_files },  /* Japan, 1998 4/14 Master Ver. */
};

static const cave_rom_load esprade_loads[] = {
	{ U42, CAVE_ROM_PROGRAM, CAVE_LOAD_EVEN, 0 },
	{ U41, CAVE_ROM_PROGRAM, CAVE_LOAD_ODD, 0 },
	{ U63, CAVE_ROM_SPRITES, CAVE_LOAD_LONG_SWAPPED, 0x000000u },
	{ U64, CAVE_ROM_SPRITES, CAVE_LOAD_LONG_SWAPPED, 0x000002u },
	{ U65, CAVE_ROM_SPRITES, CAVE_LOAD_LONG_SWAPPED, 0x800000u },
	{ U66, CAVE_ROM_SPRITES, CAVE_LOAD_LONG_SWAPPED, 0x800002u },
	{ U54, CAVE_ROM_LAYER0, CAVE_LOAD_PLAIN, 0x000000u },
	{ U55, CAVE_ROM_LAYER0, CAVE_LOAD_PLAIN, 0x400000u },
	{ U52, CAVE_ROM_LAYER1, CAVE_LOAD_PLAIN, 0x000000u },
	{ U53, CAVE_ROM_LAYER1, CAVE_LOAD_PLAIN, 0x400000u },
	{ U51, CAVE_ROM_LAYER2, CAVE_LOAD_PLAIN, 0 },
	{ U19, CAVE_ROM_SOUND0, CAVE_LOAD_PLAIN, 0 },
	{ EEPROM, CAVE_ROM_EEPROM, CAVE_LOAD_PLAIN, 0 },
};

static const cave_rom_desc esprade_roms = {
	"ESPRADE-DATA",
	{ 0x100000u, 0x1000000u, 0x800000u, 0x800000u, 0x400000u, 0, 0x400000u, 0, 0x80u },
	COUNT(world_files),
	esprade_versions, COUNT(esprade_versions),
	esprade_loads, COUNT(esprade_loads),
};

static const cave_video_desc esprade_video = {
	3, 0,                       /* three whole layers */
	0x13, -0x12,                /* (MAME's video_start: no kludge for this game) */
	0x4000, 0x4000 + 63 * 256,  /* the layers' colours from 4000; the background pen the layers' last bank, as MAME's
	                               m_background_pen */
	CAVE_SPRITES_013_ZOOM, 0x2000u,
	0x8000u,                    /* the palette: C00000-C0FFFF */
	7u,                         /* all three layers 8 bits a pixel */
	256u,                       /* the sprites' colour code a bank of 256 (granularity 256) */
	0,
	8u,                         /* the sprites 8 bits a pixel */
};

static const cave_sound_desc esprade_sound = {
	CAVE_SOUND_YMZ280B,
	{ 16934400u, 0 }, 384u,     /* (the YMZ280B: 16.9344 MHz) */
	{ 1.0, 0.0 },
	0, 0,
	{ 0u, 0u },                 /* all effects until the music and voices are told apart */
	NULL, 0,
};

/* The VBLANK's interrupt 2,000 us before the frame's end (MAME's init_esprade: m_time_vblank_irq = 2000). The
   credits for free play: not found yet (NULL: free play does nothing) */
const cave_board_desc cave_board_esprade = {
	"esprade", esprade_map, COUNT(esprade_map), &esprade_roms, &esprade_video, &esprade_sound,
	NULL, 2000u
};
