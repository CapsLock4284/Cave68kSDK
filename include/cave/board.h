/**
 *    Name:        board.h
 *    Description: a game's board, described: the Cave 68000 boards differ from game to game in where their devices
 *                 sit on the 68000's bus (MAME's cave.cpp has a memory map for each), and the SDK's bus
 *                 (machine.c) answers by the description of the game being run
 *    Version:     0.1.0 (draft)
 *    Created:     15-11-2026
 *
 *    license:BSD-3-Clause
 *    A description is data: the regions of the 68000's bus, in the order they are looked through (the first that
 *    holds an address answers it). Each region is a device and which one of its kind (a layer's number, a sound
 *    chip's). The descriptions of the games the SDK knows: cave/boards.h.
 */
#ifndef CAVE_BOARD_H
#define CAVE_BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What answers at a region of the bus */
typedef enum
{
	CAVE_REGION_PROGRAM,        /* the program ROM: read */
	CAVE_REGION_WORK_RAM,       /* the work RAM */
	CAVE_REGION_LAYER_RAM,      /* a tile layer's RAM ('unit': the layer; 'offset' words into it; 'mask' the address
	                               bits used, for a mirrored RAM: 0 for the region's own size) */
	CAVE_REGION_LAYER_REGS,     /* a tile layer's 3 registers ('unit': the layer) */
	CAVE_REGION_SPRITE_RAM,     /* the sprites' RAM */
	CAVE_REGION_VIDEO_REGS,     /* the video registers: written; read, the first 4 words are the interrupt's cause
	                               (reading word 2 clears the VBLANK's, word 3 the other's), the rest 0 */
	CAVE_REGION_IGNORED,        /* written and ignored (the watchdog): put before the region it is in */
	CAVE_REGION_PALETTE,        /* the palette RAM */
	CAVE_REGION_OKI,            /* an OKI M6295 ('unit': which): its status read, its commands written, the word's
	                               low byte */
	CAVE_REGION_NMK112,         /* the NMK112's bank registers: written, the word's low byte */
	CAVE_REGION_YMZ280B,        /* the YMZ280B: word 0 its register number, word 1 the register's data (written) or its
	                               status (read), the word's low byte */
	CAVE_REGION_INPUTS,         /* the inputs ('unit' 0 or 1: IN0, IN1); IN1 has the EEPROM's data out (bit 11) */
	CAVE_REGION_EEPROM          /* the EEPROM's lines: written (a byte write's byte; bit 3 data in, 1 select,
	                               2 clock) */
} cave_region_kind;

typedef struct
{
	uint32_t start, end;        /* the addresses, 'end' not included */
	cave_region_kind kind;
	uint8_t unit;
	uint16_t offset;
	uint32_t mask;
} cave_region;

/* ---- the ROM set: its files, and where each goes ---- */

/* The regions the ROM set fills (a game's description gives each one's size: 0 for one it has not) */
typedef enum
{
	CAVE_ROM_PROGRAM,           /* the 68000's program, big endian as it sees it */
	CAVE_ROM_SPRITES,           /* the sprites' graphics, in the board's order */
	CAVE_ROM_LAYER0,            /* the tile layers' graphics, one region a layer */
	CAVE_ROM_LAYER1,
	CAVE_ROM_LAYER2,
	CAVE_ROM_LAYER3,
	CAVE_ROM_SOUND0,            /* the sound chips' samples, one region a chip */
	CAVE_ROM_SOUND1,
	CAVE_ROM_EEPROM,            /* the EEPROM's first contents: 128 bytes, its 64 words big endian */
	CAVE_ROM_REGIONS
} cave_rom_region;

typedef struct
{
	const char *name;           /* MAME's name for it */
	uint32_t crc, size;         /* how it is known, whatever its name */
} cave_rom_file;

/* How a file's bytes go into its region */
typedef enum
{
	CAVE_LOAD_PLAIN,            /* as they are (MAME's ROM_LOAD) */
	CAVE_LOAD_SWAPPED,          /* swapped in pairs (ROM_LOAD16_WORD_SWAP) */
	CAVE_LOAD_EVEN,             /* to every other byte from 'offset': the 16-bit words' high bytes (ROM_LOAD16_BYTE
	                               at an even offset) */
	CAVE_LOAD_ODD               /* the same from 'offset' + 1: the words' low bytes */
} cave_load_mode;

/* A file's place: file 'file' (in the version's list) put at 'offset' in 'region', as 'mode' says
   (cave_load_mode) */
typedef struct
{
	uint8_t file, region, mode;
	uint32_t offset;
} cave_rom_load;

/* A version of the game (one of MAME's sets): its files, in the same order in every version, so that the loads
   are the same; a clone's files that are its parent's are the same entries */
typedef struct
{
	const char *mame_name;      /* "donpachi" */
	const char *screen_name;    /* "USA" */
	int parent;                 /* the version whose .zip has the files a clone shares (MAME's parent), or -1 */
	const cave_rom_file *files;
} cave_rom_version;

typedef struct
{
	const char *data_magic;     /* the extracted data's header (at most 15 characters) */
	uint32_t region_size[CAVE_ROM_REGIONS];
	int file_count;
	const cave_rom_version *versions;
	int version_count;
	const cave_rom_load *loads;
	int load_count;
} cave_rom_desc;

/* ---- the picture ---- */

typedef enum
{
	CAVE_SPRITES_013            /* the 013: 8 words a sprite, no zoom (DonPachi's) */
} cave_sprite_format;

typedef struct
{
	int layers;                 /* the 038 tile layers: 1 to 3 */
	uint8_t small_only;         /* the layers (bit n: layer n) whose tiles are always 8x8 */
	int layer_offs_x, layer_offs_y;    /* MAME's m_layers_offs_x, _y */
	uint16_t layer_colour_base; /* the layers' first pen (64 palettes of 16 from it) */
	uint16_t background_pen;
	cave_sprite_format sprites;
	uint32_t sprite_bank_words; /* a bank of the sprite RAM (the program shows one by video register 4) */
	uint32_t palette_words;     /* the palette RAM's words (at most CAVE_PALETTE_WORDS) */
	uint8_t eight_bits;         /* the layers (bit n: layer n) whose tiles have 8 bits a pixel (a byte each, 64 bytes
	                               an 8x8 tile), their colour's palettes of 256 */
	uint16_t colour_step;       /* the colours a colour code steps by, for the sprites and the 4-bit layers: 16
	                               (DonPachi: 64 palettes of 16), or 256 (DoDonPachi: each code a bank of 256, its first
	                               16 used); 0 is 16 */
	uint8_t sprite_low_first;   /* the sprites' ROM bytes hold their two pixels low nibble first (1) or high first
	                               (0: DonPachi's, its ROMs word swapped) */
} cave_video_desc;

/* ---- the sound ---- */

typedef enum
{
	CAVE_SOUND_OKI_NMK112,      /* two OKI M6295s behind an NMK112's banks */
	CAVE_SOUND_YMZ280B          /* one Yamaha YMZ280B (8 voices of ADPCM or PCM), its samples in CAVE_ROM_SOUND0;
	                               its interrupt is the 68000's level 1, with the VBLANK's (cave/sound.h) */
} cave_sound_kind;

typedef struct
{
	cave_sound_kind kind;
	uint32_t clock[2];          /* each chip's clock, Hz (the YMZ280B: clock[0]) */
	uint16_t divisor;           /* clocks a sample (132: pin 7 high) */
	double gain[2];             /* each chip's level in the mix */
	uint32_t skip;              /* the empty bytes at the start of each chip's region (the NMK112's old layout) */
	uint8_t paged_table;        /* the chips (bit n: chip n) whose sample table the NMK112 pages too */
} cave_sound_desc;

/* ---- the game's credits (cave/free_play.h) ---- */

typedef struct
{
	uint32_t credits;           /* the work RAM word the game counts its credits in (a coin adds, a start takes) */
	uint32_t two_credits;       /* the byte whose bit 'two_credits_bit' set makes a game take 2 credits (the test
	                               mode's setting, read from the EEPROM), clear 1 */
	uint8_t two_credits_bit;
} cave_credits_desc;

typedef struct
{
	const char *name;           /* MAME's machine name ("donpachi") */
	const cave_region *map;     /* the bus */
	int map_count;
	const cave_rom_desc *roms;
	const cave_video_desc *video;
	const cave_sound_desc *sound;
	const cave_credits_desc *credits;    /* the program's credits, for free play (NULL: free play does nothing) */
} cave_board_desc;

/* The board being run: its description (DonPachi's until a game chooses its own). Set it before cave_reset */
extern const cave_board_desc *cave_board_description;
void cave_board_describe(const cave_board_desc *description);

#ifdef __cplusplus
}
#endif

#endif
