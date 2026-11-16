/**
 *    Name:        ddonpach.c
 *    Description: DoDonPachi's board described (cave/board.h): its bus, ROM set, picture and sound
 *    Version:     0.1.0 (draft)
 *    Created:     15-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/atlus/cave.cpp (BSD-3-Clause, Luca Elia and others): ddonpach_map, ROM_START( ddonpach )
 *    and ROM_START( ddonpachj ).
 *      000000-0FFFFF  program ROM (two chips, u27 the words' high bytes)     100000-10FFFF  work RAM
 *      300000-300003  YMZ280B           400000-40FFFF  sprite RAM
 *      500000-507FFF  layer 0 (038)     600000-607FFF  layer 1 (038)     700000-703FFF  layer 2, 8x8 only (its RAM's
 *                                                                        last 16 KB, mirrored to 70FFFF)
 *      800000-80007F  video registers (800000-800007 read: the interrupt's cause)
 *      900000/A00000/B00000  layers 0, 1, 2: their 3 registers     C00000-C0FFFF  palette
 *      D00000  inputs 1     D00002  inputs 2 + the EEPROM's data out (bit 11)     E00000  EEPROM lines
 *    The program's chips checked by its reset vector: u27 as the high bytes gives the stack at 110000 and the start
 *    at 0052EA (the World set; 0052F6 the Japanese), the other way round nonsense.
 *
 *    Checked by the pictures: layer 2's 8 bits a pixel (MAME's layout_8x8x8: the warning screen reads); the palette's
 *    banks of 256 (the program fills 00-3F for the sprites, 40-7F for the layers, full banks for layer 2); the
 *    sprites' ROMs word swapped as DonPachi's (the title's letters read only that way: u50-u53 as they are give
 *    broken glyphs). The stage backgrounds are sprites (layers 0 and 1 stay empty in the demo).
 *    Not checked yet: the layers' offsets, the samples' order (u6 then u7), and whether 800078 is a watchdog the
 *    interrupt writes, as DonPachi's 900078 is.
 */
#include "cave/boards.h"

static const cave_region ddonpach_map[] = {
	{ 0x000000u, 0x100000u, CAVE_REGION_PROGRAM, 0, 0, 0 },
	{ 0x100000u, 0x110000u, CAVE_REGION_WORK_RAM, 0, 0, 0 },
	{ 0x300000u, 0x300004u, CAVE_REGION_YMZ280B, 0, 0, 0 },
	{ 0x400000u, 0x410000u, CAVE_REGION_SPRITE_RAM, 0, 0, 0 },
	{ 0x500000u, 0x508000u, CAVE_REGION_LAYER_RAM, 0, 0, 0 },
	{ 0x600000u, 0x608000u, CAVE_REGION_LAYER_RAM, 1, 0, 0 },
	{ 0x700000u, 0x710000u, CAVE_REGION_LAYER_RAM, 2, 0x2000u, 0x3FFFu },
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

/* the files, in the same order in both versions: the Japanese set's own program, the rest the World set's */
enum { U27, U26, U50, U51, U52, U53, U60, U61, U62, U6, U7, EEPROM };

static const cave_rom_file world_files[] = {
	{ "b1.u27", 0xb5cdc8d3u, 0x080000u },
	{ "b2.u26", 0x6bbb063au, 0x080000u },
	{ "u50.bin", 0x14b260ecu, 0x200000u },
	{ "u51.bin", 0xe7ba8cceu, 0x200000u },
	{ "u52.bin", 0x02492ee0u, 0x200000u },
	{ "u53.bin", 0xcb4c10f0u, 0x200000u },
	{ "u60.bin", 0x903096a7u, 0x200000u },
	{ "u61.bin", 0xd89b7631u, 0x200000u },
	{ "u62.bin", 0x292bfb6bu, 0x200000u },
	{ "u6.bin", 0x9dfdafafu, 0x200000u },
	{ "u7.bin", 0x795b17d5u, 0x200000u },
	{ "eeprom-ddonpach.bin", 0x315fb546u, 0x000080u },
};

static const cave_rom_file japan_files[] = {
	{ "u27.bin", 0x2432ff9bu, 0x080000u },
	{ "u26.bin", 0x4f3a914au, 0x080000u },
	{ "u50.bin", 0x14b260ecu, 0x200000u },
	{ "u51.bin", 0xe7ba8cceu, 0x200000u },
	{ "u52.bin", 0x02492ee0u, 0x200000u },
	{ "u53.bin", 0xcb4c10f0u, 0x200000u },
	{ "u60.bin", 0x903096a7u, 0x200000u },
	{ "u61.bin", 0xd89b7631u, 0x200000u },
	{ "u62.bin", 0x292bfb6bu, 0x200000u },
	{ "u6.bin", 0x9dfdafafu, 0x200000u },
	{ "u7.bin", 0x795b17d5u, 0x200000u },
	{ "eeprom-ddonpach.bin", 0x315fb546u, 0x000080u },
};

static const cave_rom_version ddonpach_versions[] = {
	{ "ddonpach", "WORLD", -1, world_files },    /* International, Master Ver. 97/02/05 */
	{ "ddonpachj", "JAPAN", 0, japan_files },    /* Japan, Master Ver. 97/02/05 */
};

static const cave_rom_load ddonpach_loads[] = {
	{ U27, CAVE_ROM_PROGRAM, CAVE_LOAD_EVEN, 0 },
	{ U26, CAVE_ROM_PROGRAM, CAVE_LOAD_ODD, 0 },
	{ U50, CAVE_ROM_SPRITES, CAVE_LOAD_SWAPPED, 0x000000u },
	{ U51, CAVE_ROM_SPRITES, CAVE_LOAD_SWAPPED, 0x200000u },
	{ U52, CAVE_ROM_SPRITES, CAVE_LOAD_SWAPPED, 0x400000u },
	{ U53, CAVE_ROM_SPRITES, CAVE_LOAD_SWAPPED, 0x600000u },
	{ U60, CAVE_ROM_LAYER0, CAVE_LOAD_PLAIN, 0 },
	{ U61, CAVE_ROM_LAYER1, CAVE_LOAD_PLAIN, 0 },
	{ U62, CAVE_ROM_LAYER2, CAVE_LOAD_PLAIN, 0 },
	{ U6, CAVE_ROM_SOUND0, CAVE_LOAD_PLAIN, 0x000000u },
	{ U7, CAVE_ROM_SOUND0, CAVE_LOAD_PLAIN, 0x200000u },
	{ EEPROM, CAVE_ROM_EEPROM, CAVE_LOAD_PLAIN, 0 },
};

static const cave_rom_desc ddonpach_roms = {
	"DDONPACH-DATA",
	{ 0x100000u, 0x800000u, 0x200000u, 0x200000u, 0x200000u, 0, 0x400000u, 0, 0x80u },
	COUNT(world_files),
	ddonpach_versions, COUNT(ddonpach_versions),
	ddonpach_loads, COUNT(ddonpach_loads),
};

static const cave_video_desc ddonpach_video = {
	3, 1u << 2,                 /* layer 2 (the text): 8x8 tiles only */
	0x13, -0x12,                /* (DonPachi's: to be checked) */
	0x4000, 0x4000 + 63 * 256,  /* the layers' colours from 4000 (the palette's banks 40-7F; the program fills the
	                               sprites' at 00-3F, and full banks for layer 2's 8-bit tiles) */
	CAVE_SPRITES_013, 0x2000u,
	0x8000u,                    /* the palette: C00000-C0FFFF */
	1u << 2,                    /* layer 2: 8 bits a pixel (u62) */
	256u,                       /* a colour code a bank of 256 (the sprites' and 4-bit layers' first 16 used) */
	0,                          /* the sprites' pixels high nibble first (their ROMs word swapped), as DonPachi's */
};

static const cave_sound_desc ddonpach_sound = {
	CAVE_SOUND_YMZ280B,
	{ 16934400u, 0 }, 384u,     /* (the YMZ280B: 16.9344 MHz) */
	{ 1.0, 0.0 },
	0, 0,
};

const cave_board_desc cave_board_ddonpach = {
	"ddonpach", ddonpach_map, COUNT(ddonpach_map), &ddonpach_roms, &ddonpach_video, &ddonpach_sound
};
