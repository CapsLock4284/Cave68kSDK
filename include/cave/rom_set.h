/**
 *    Name:        rom_set.h
 *    Description: the player's ROM set, from its .zip or a folder of its files, as the game's description
 *                 (cave/board.h's cave_rom_desc) lists them: for DonPachi MAME's 'donpachi' (US, Ver. 1.12) or
 *                 'donpachij' (Japan, Ver. 1.01: its own program and text chips, the others the US set's)
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    The regions as MAME's ROM_START()s in src/mame/cave/cave.cpp lay them out (BSD-3-Clause).
 */
#ifndef CAVE_ROM_SET_H
#define CAVE_ROM_SET_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The set loaded: its regions (cave/board.h's cave_rom_region), as the game's description lays them out; NULL for a
   region the game has not */
typedef struct
{
	int version;          /* which of the description's versions (0: the first, MAME's parent set) */
	uint8_t *program;     /* CAVE_ROM_PROGRAM: big endian, as the 68000 sees it */
	uint8_t *sprites;     /* CAVE_ROM_SPRITES */
	uint8_t *layer[4];    /* CAVE_ROM_LAYER0-3 */
	uint8_t *oki[2];      /* CAVE_ROM_SOUND0-1: the sound chips' samples */
	uint16_t eeprom[64];  /* CAVE_ROM_EEPROM, as words */
} cave_rom_set;

extern cave_rom_set rom_set;

/* A region's size, by the description of the board being run (0: the game has not that region) */
uint32_t rom_set_region_size(int region);

#define ROM_SET_FILES_MOST 32    /* the files of a version, at most */

/* ---- DonPachi's names (the first game described, src/boards/donpachi.c), kept for its port ---- */

#define DONPACHI_PROGRAM_SIZE  0x080000u
#define DONPACHI_SPRITES_SIZE  0x400000u
#define DONPACHI_LAYER01_SIZE  0x100000u
#define DONPACHI_LAYER2_SIZE   0x040000u
#define DONPACHI_OKI1_SIZE     0x240000u
#define DONPACHI_OKI2_SIZE     0x340000u
#define ROM_SET_FILES          9

typedef int donpachi_version;
enum
{
	DONPACHI_US,        /* 'donpachi', Ver. 1.12 1995/05/2x */
	DONPACHI_JAPAN,     /* 'donpachij', Ver. 1.01 1995/05/11 */
	DONPACHI_VERSIONS
};
typedef cave_rom_set donpachi_rom_set;

/* ---- reading it ---- */

/* Reads the first version's set (MAME's parent) from 'source', a .zip or a folder (its files by MAME's names, or the
   set's .zip in it): NULL, or what is wrong. rom_set_load_version: the version's (a clone: from 'source', its loose
   files, its .zip and its parent's in the folder, or a .zip and the parent's beside it: MAME's clone set leaves the
   shared files to its parent). Each file is known by its CRC32 and size, whatever its name or the .zip it is in
   (merged sets too) */
const char *rom_set_load(const char *source);
const char *rom_set_load_version(const char *source, int version);

/* A clone's set over its parent's now loaded: only the clone's own files (DonPachi's Japanese set: its program and
   text, prg.u29 and u58.bin) read from 'source' (a .zip, the clone's alone will do, or a folder: its loose files or
   its .zips), the parent's other regions kept (the chips the two share): NULL, or what is wrong (not the parent's set
   loaded; the clone's files not there: rom_set_files_missing names them). So a clone needs no more than its own .zip
   once the parent's data is there. rom_set_load_japan_over: DonPachi's Japanese set's */
const char *rom_set_load_clone_over(const char *source, int version);
const char *rom_set_load_japan_over(const char *source);

/* The regions freed (rom_set empty again: another set to load) */
void rom_set_free(void);

/* The version's name for the screen ("USA", "JAPAN") and MAME's set ("donpachi", "donpachij"); how many versions */
const char *rom_set_version_name(int version);
const char *rom_set_mame_name(int version);
int rom_set_versions(void);

/* The files of a version (MAME's names) */
int rom_set_files(void);

/* After rom_set_load (of the version it was asked for): how many of the set's files it found, and the names of
   those it did not (at most 'most' in 'names'; 'different': a file of that name was there, but not the right one:
   another version, or damaged): how many it did not find */
int rom_set_files_found(void);
int rom_set_files_missing(const char **names, bool *different, int most);

/* The game's data, extracted: the set as read (its regions, in the order above), kept in one file the program
   reads instead of the ROM set from then on. Never part of the repository (it is the ROMs' contents).
   rom_set_save_data writes it (after rom_set_load: its version in the header); rom_set_load_data reads it, checked by
   its CRC32 and that it is the first version's (rom_set_load_data_version: the version's): NULL, or what is wrong (no file,
   another program's or version's, damaged) */
const char *rom_set_save_data(const char *path);
const char *rom_set_load_data(const char *path);
const char *rom_set_load_data_version(const char *path, int version);

#ifdef __cplusplus
}
#endif

#endif
