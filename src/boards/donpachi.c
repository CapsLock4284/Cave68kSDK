/**
 *    Name:        donpachi.c
 *    Description: DonPachi's board described (cave/board.h): its bus, ROM set, picture and sound
 *    Version:     0.1.0 (draft)
 *    Created:     15-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/cave/cave.cpp (BSD-3-Clause, Luca Elia and others): donpachi_map. In the order the
 *    regions are looked through (the busiest first, then as the map has them).
 *      000000-07FFFF  program ROM          100000-10FFFF  work RAM
 *      200000-207FFF  layer 1 (038)        300000-307FFF  layer 0 (038)    400000-407FFF  layer 2, 8x8 only (its
 *                                                                          RAM's last 16 KB, mirrored)
 *      500000-50FFFF  sprite RAM           600000/700000/800000  layers 1, 0, 2: their 3 registers
 *      900000-90007F  video registers (900000-900007 read: the interrupt's cause; 900078: the watchdog)
 *      A08000-A08FFF  palette (xGRB 555)   B00000/B00010  M6295 #0, #1    B00020-B0002F  NMK112 banks
 *      C00000  inputs 1     C00002  inputs 2 + the EEPROM's data out (bit 11)     D00000  EEPROM lines
 *    The ROM sets: ROM_START( donpachi ) and ROM_START( donpachij ). The picture: video_start (m_layers_offs_x,
 *    _y), gfx_donpachi. The sound: the donpachi machine (#0 at 4.220 MHz / 4, #1 at 4.220 MHz / 2, both with pin 7
 *    high; #0 mixed at 1.60, #1 at 1.0; the NMK112 pages #1's sample table, not #0's).
 */
#include "cave/boards.h"

static const cave_region donpachi_map[] = {
	{ 0x000000u, 0x080000u, CAVE_REGION_PROGRAM, 0, 0, 0 },
	{ 0x100000u, 0x110000u, CAVE_REGION_WORK_RAM, 0, 0, 0 },
	{ 0x200000u, 0x208000u, CAVE_REGION_LAYER_RAM, 1, 0, 0 },
	{ 0x300000u, 0x308000u, CAVE_REGION_LAYER_RAM, 0, 0, 0 },
	{ 0x400000u, 0x408000u, CAVE_REGION_LAYER_RAM, 2, 0x2000u, 0x3FFFu },
	{ 0x500000u, 0x510000u, CAVE_REGION_SPRITE_RAM, 0, 0, 0 },
	{ 0x600000u, 0x600006u, CAVE_REGION_LAYER_REGS, 1, 0, 0 },
	{ 0x700000u, 0x700006u, CAVE_REGION_LAYER_REGS, 0, 0, 0 },
	{ 0x800000u, 0x800006u, CAVE_REGION_LAYER_REGS, 2, 0, 0 },
	{ 0x900078u, 0x90007Au, CAVE_REGION_IGNORED, 0, 0, 0 },
	{ 0x900000u, 0x900080u, CAVE_REGION_VIDEO_REGS, 0, 0, 0 },
	{ 0xA08000u, 0xA09000u, CAVE_REGION_PALETTE, 0, 0, 0 },
	{ 0xB00000u, 0xB00004u, CAVE_REGION_OKI, 0, 0, 0 },
	{ 0xB00010u, 0xB00014u, CAVE_REGION_OKI, 1, 0, 0 },
	{ 0xB00020u, 0xB00030u, CAVE_REGION_NMK112, 0, 0, 0 },
	{ 0xC00000u, 0xC00002u, CAVE_REGION_INPUTS, 0, 0, 0 },
	{ 0xC00002u, 0xC00004u, CAVE_REGION_INPUTS, 1, 0, 0 },
	{ 0xD00000u, 0xD00002u, CAVE_REGION_EEPROM, 0, 0, 0 },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* the files, in the same order in both versions (the loads below count on it): the Japanese set's own program
   and text, the rest the US set's */
enum { U29, U44, U45, U54, U57, U58, U32, U33, U10 };

static const cave_rom_file us_files[] = {
	{ "prgu.u29", 0x89c36802u, 0x080000u },
	{ "atdp.u44", 0x7189e953u, 0x200000u },
	{ "atdp.u45", 0x6984173fu, 0x200000u },
	{ "atdp.u54", 0x6bda6b66u, 0x100000u },
	{ "atdp.u57", 0x0a0e72b9u, 0x100000u },
	{ "text.u58", 0x5dba06e7u, 0x040000u },
	{ "atdp.u32", 0x0d89fccau, 0x100000u },
	{ "atdp.u33", 0xd749de00u, 0x200000u },
	{ "eeprom-donpachi.u10", 0x315fb546u, 0x000080u },
};

static const cave_rom_file japan_files[] = {
	{ "prg.u29", 0x6be14af6u, 0x080000u },
	{ "atdp.u44", 0x7189e953u, 0x200000u },
	{ "atdp.u45", 0x6984173fu, 0x200000u },
	{ "atdp.u54", 0x6bda6b66u, 0x100000u },
	{ "atdp.u57", 0x0a0e72b9u, 0x100000u },
	{ "u58.bin", 0x285379ffu, 0x040000u },
	{ "atdp.u32", 0x0d89fccau, 0x100000u },
	{ "atdp.u33", 0xd749de00u, 0x200000u },
	{ "eeprom-donpachi.u10", 0x315fb546u, 0x000080u },
};

static const cave_rom_version donpachi_versions[] = {
	{ "donpachi", "USA", -1, us_files },        /* Ver. 1.12 1995/05/2x */
	{ "donpachij", "JAPAN", 0, japan_files },   /* Ver. 1.01 1995/05/11 */
};

static const cave_rom_load donpachi_loads[] = {
	{ U29, CAVE_ROM_PROGRAM, 1, 0 },
	{ U44, CAVE_ROM_SPRITES, 1, 0 },
	{ U45, CAVE_ROM_SPRITES, 1, 0x200000u },
	{ U54, CAVE_ROM_LAYER0, 0, 0 },
	{ U57, CAVE_ROM_LAYER1, 0, 0 },
	{ U58, CAVE_ROM_LAYER2, 0, 0 },
	{ U33, CAVE_ROM_SOUND0, 0, 0x040000u },     /* (after 0x40000 empty bytes, the NMK112's old layout) */
	{ U32, CAVE_ROM_SOUND1, 0, 0x040000u },
	{ U33, CAVE_ROM_SOUND1, 0, 0x140000u },
	{ U10, CAVE_ROM_EEPROM, 0, 0 },
};

static const cave_rom_desc donpachi_roms = {
	"DONPACHI-DATA",
	{ 0x080000u, 0x400000u, 0x100000u, 0x100000u, 0x040000u, 0, 0x240000u, 0x340000u, 0x80u },
	COUNT(us_files),
	donpachi_versions, COUNT(donpachi_versions),
	donpachi_loads, COUNT(donpachi_loads),
};

static const cave_video_desc donpachi_video = {
	3, 1u << 2,                 /* layer 2 (the text): 8x8 tiles only */
	0x13, -0x12,
	0x400, 0x400 + 63 * 16,
	CAVE_SPRITES_013, 0x2000u,
	0x800u,                     /* the palette: A08000-A08FFF */
};

/* The voices (the owner's choice, by ear, of the long or loud sounds #1 plays a few times a game: logged through both
   loops, tests/harness/sfx_log's way): #1's other sounds are effects; #0's are all music */
static const cave_sound_sample donpachi_voices[] = {
	{ 1, 0x008D6Au, 2 },
	{ 1, 0x037099u, 2 },
	{ 1, 0x039EA1u, 2 },
	{ 1, 0x04CBD9u, 2 },
	{ 1, 0x06AA3Bu, 2 },
	{ 1, 0x084504u, 2 },
	{ 1, 0x08ABB0u, 2 },
	{ 1, 0x090400u, 2 },
	{ 1, 0x092D7Au, 2 },
	{ 1, 0x096427u, 2 },
	{ 1, 0x09B681u, 2 },
	{ 1, 0x0A0400u, 2 },
	{ 1, 0x0AB5F3u, 2 },
	{ 1, 0x0B0400u, 2 },
	{ 1, 0x0B69E8u, 2 },
	{ 1, 0x0BA5EEu, 2 },
	{ 1, 0x0C0400u, 2 },
	{ 1, 0x0C6FE5u, 2 },
	{ 1, 0x0CDC6Eu, 2 },
	{ 1, 0x0D0400u, 2 },
	{ 1, 0x0D7FCEu, 2 },
	{ 1, 0x0E0400u, 2 },
	{ 1, 0x0E627Fu, 2 },
	{ 1, 0x0ED620u, 2 },
	{ 1, 0x0F0400u, 2 },
	{ 1, 0x0F66A5u, 2 }
};

static const cave_sound_desc donpachi_sound = {
	CAVE_SOUND_OKI_NMK112,
	{ 4220000u / 4u, 4220000u / 2u }, 132u,
	{ 1.60, 1.0 },
	0x040000u, 1u << 1,
	{ 1u, 0u },                 /* #0 music, #1 effects (and its voices) */
	donpachi_voices, COUNT(donpachi_voices),
};

/* The credits: the word at $10139A (the coin routine at 00CBDA adds to it; MAME's "Infinite Credits" cheat writes
   it); bit 0 of $101287, the test mode's coin setting read from the EEPROM at 034488, makes a game take 2 credits
   (034746) */
static const cave_credits_desc donpachi_credits = { 0x10139Au, 0x101287u, 0 };

/* The VBLANK's interrupt 90 us before the frame's end (MAME's init_donpachi: m_time_vblank_irq = 90) */
const cave_board_desc cave_board_donpachi = {
	"donpachi", donpachi_map, COUNT(donpachi_map), &donpachi_roms, &donpachi_video, &donpachi_sound,
	&donpachi_credits, 90u
};
