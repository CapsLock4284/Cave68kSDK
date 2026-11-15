/**
 *    Name:        rom_set.c
 *    Description: the player's ROM set into memory, from a .zip or a folder, as the game's description lists it
 *    Version:     0.2.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    Each file is known by its CRC32 and size, whatever its name. The .zip reading is tmntarcade-decomp's
 *    runtime/src/rom_set.c (BSD-3-Clause, Sebastian Toyos: runtime/LICENSE.tmnt-runtime), with inflate.c; the
 *    regions follow the ROM_START()s of MAME's src/mame/cave/cave.cpp (BSD-3-Clause), as cave/board.h describes them.
 */
#include "cave/rom_set.h"

#include "cave/board.h"
#include "inflate.h"

#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZIP_END             0x06054B50u
#define ZIP_ENTRY           0x02014B50u
#define ZIP_LOCAL           0x04034B50u
#define ZIP_STORED          0
#define ZIP_DEFLATED        8
#define ZIP_COMMENT_MOST    0xFFFFu

cave_rom_set rom_set;

/* the description's ROM set, and the files of the version being read */
#define ROMS (cave_board_description->roms)
static const cave_rom_file *files;
static int files_count;

/* what the last rom_set_load found of each file */
enum { FILE_MISSING, FILE_FOUND, FILE_DIFFERENT };
static int file_state[ROM_SET_FILES_MOST];
static bool name_seen[ROM_SET_FILES_MOST];    /* (a file of its name, right or not) */

static int use_version(int version)
{
	if (!ROMS || version < 0 || version >= ROMS->version_count || ROMS->file_count > ROM_SET_FILES_MOST)
		return 0;
	files = ROMS->versions[version].files;
	files_count = ROMS->file_count;
	return 1;
}

int rom_set_versions(void)
{
	return ROMS ? ROMS->version_count : 0;
}

int rom_set_files(void)
{
	return ROMS ? ROMS->file_count : 0;
}

const char *rom_set_version_name(int version)
{
	return ROMS && version >= 0 && version < ROMS->version_count ? ROMS->versions[version].screen_name : "";
}

const char *rom_set_mame_name(int version)
{
	return ROMS && version >= 0 && version < ROMS->version_count ? ROMS->versions[version].mame_name : "";
}

uint32_t rom_set_region_size(int region)
{
	return ROMS && region >= 0 && region < CAVE_ROM_REGIONS ? ROMS->region_size[region] : 0u;
}

/* a region's place in a set (NULL: the EEPROM, kept as words) */
static uint8_t **region_of(cave_rom_set *set, int region)
{
	switch (region)
	{
	case CAVE_ROM_PROGRAM: return &set->program;
	case CAVE_ROM_SPRITES: return &set->sprites;
	case CAVE_ROM_LAYER0: case CAVE_ROM_LAYER1: case CAVE_ROM_LAYER2: case CAVE_ROM_LAYER3:
		return &set->layer[region - CAVE_ROM_LAYER0];
	case CAVE_ROM_SOUND0: case CAVE_ROM_SOUND1: return &set->oki[region - CAVE_ROM_SOUND0];
	default: return NULL;
	}
}

static void free_set(cave_rom_set *set)
{
	for (int region = 0; region < CAVE_ROM_REGIONS; region++)
		if (region_of(set, region))
			free(*region_of(set, region));
	memset(set, 0, sizeof(*set));
}

static uint32_t crc32_of(const uint8_t *data, uint32_t size)
{
	static uint32_t table[256];
	uint32_t crc = 0xFFFFFFFFu, index;
	int bit;

	if (table[1] == 0u)
		for (index = 0; index < 256u; index++)
		{
			table[index] = index;
			for (bit = 0; bit < 8; bit++)
				table[index] = (table[index] >> 1) ^ (0xEDB88320u & (0u - (table[index] & 1u)));
		}
	for (index = 0; index < size; index++)
		crc = (crc >> 8) ^ table[(crc ^ data[index]) & 0xFFu];
	return ~crc;
}

static uint32_t little16(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

static uint32_t little32(const uint8_t *bytes)
{
	return little16(bytes) | little16(bytes + 2) << 16;
}

static uint8_t *read_whole(const char *path, size_t *size)
{
	FILE *input = fopen(path, "rb");
	uint8_t *data = NULL;
	long length;

	if (!input)
		return NULL;
	if (fseek(input, 0, SEEK_END) == 0 && (length = ftell(input)) > 0 && fseek(input, 0, SEEK_SET) == 0
		&& (data = malloc((size_t)length)) != NULL && fread(data, 1, (size_t)length, input) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(input);
	if (data)
		*size = (size_t)length;
	return data;
}

/* a .zip entry's name ('length' bytes, maybe in a folder) is the set's file 'file' (in any case) */
static bool same_name(const uint8_t *name, size_t length, int file)
{
	const size_t want = strlen(files[file].name);
	size_t start = length;

	while (start > 0 && name[start - 1] != '/' && name[start - 1] != '\\')
		start--;
	if (length - start != want)
		return false;
	for (size_t at = 0; at < want; at++)
		if (tolower(name[start + at]) != files[file].name[at])
			return false;
	return true;
}

/* The set's files in the .zip 'zip' into 'chips', by their CRC32: 0 when it is no .zip */
static int read_zip(const uint8_t *zip, size_t size, uint8_t **chips)
{
	size_t end, entry, local;
	uint32_t entries, index;
	int file;

	if (size < 22)
		return 0;
	for (end = size - 22;; end--)
	{
		if (little32(zip + end) == ZIP_END)
			break;
		if (end == 0 || size - end > 22 + ZIP_COMMENT_MOST)
			return 0;
	}
	entries = little16(zip + end + 10);
	entry = little32(zip + end + 16);
	for (index = 0; index < entries; index++)
	{
		uint32_t method, crc, packed, unpacked;

		if (entry > size || size - entry < 46 || little32(zip + entry) != ZIP_ENTRY)
			return 0;
		method = little16(zip + entry + 10);
		crc = little32(zip + entry + 16);
		packed = little32(zip + entry + 20);
		unpacked = little32(zip + entry + 24);
		local = little32(zip + entry + 42);
		for (file = 0; file < files_count; file++)
			if (size - entry >= 46u + little16(zip + entry + 28)
					&& same_name(zip + entry + 46, little16(zip + entry + 28), file))
				name_seen[file] = true;
		entry += 46 + little16(zip + entry + 28) + little16(zip + entry + 30) + little16(zip + entry + 32);
		for (file = 0; file < files_count; file++)
			if (!chips[file] && files[file].crc == crc && files[file].size == unpacked)
				break;
		if (file == files_count || local > size || size - local < 30 || little32(zip + local) != ZIP_LOCAL)
			continue;
		local += 30 + little16(zip + local + 26) + little16(zip + local + 28);
		if (local > size || packed > size - local || (method == ZIP_STORED && packed != unpacked)
			|| !(chips[file] = malloc(unpacked)))
			continue;
		if ((method == ZIP_STORED ? (memcpy(chips[file], zip + local, unpacked), 1)
				: method == ZIP_DEFLATED && inflate_raw(zip + local, packed, chips[file], unpacked))
			&& crc32_of(chips[file], unpacked) == crc)
			continue;
		free(chips[file]);
		chips[file] = NULL;
	}
	return 1;
}

static void read_folder(const char *folder, uint8_t **chips)
{
	char path[1024];

	for (int file = 0; file < files_count; file++)
	{
		size_t size = 0;

		snprintf(path, sizeof(path), "%s/%s", folder, files[file].name);
		chips[file] = read_whole(path, &size);
		if (chips[file])
			name_seen[file] = true;
		if (chips[file] && (size != files[file].size || crc32_of(chips[file], files[file].size) != files[file].crc))
		{
			free(chips[file]);
			chips[file] = NULL;
		}
	}
}

int rom_set_files_found(void)
{
	int found = 0;

	for (int file = 0; file < files_count; file++)
		found += file_state[file] == FILE_FOUND;
	return found;
}

int rom_set_files_missing(const char **names, bool *different, int most)
{
	int count = 0;

	for (int file = 0; file < files_count; file++)
		if (file_state[file] != FILE_FOUND)
		{
			if (count < most)
			{
				names[count] = files[file].name;
				different[count] = file_state[file] == FILE_DIFFERENT;
			}
			count++;
		}
	return count;
}

/* the files still missing from the .zip 'path', if it is there */
static void read_zip_file(const char *path, uint8_t **chips)
{
	size_t size = 0;
	uint8_t *const whole = read_whole(path, &size);

	if (whole)
	{
		read_zip(whole, size, chips);
		free(whole);
	}
}

static bool all_found(uint8_t **chips)
{
	for (int file = 0; file < files_count; file++)
		if (!chips[file])
			return false;
	return true;
}

static void free_chips(uint8_t **chips)
{
	for (int file = 0; file < files_count; file++)
	{
		free(chips[file]);
		chips[file] = NULL;
	}
}

/* a file's bytes to its place in 'set' (the regions already there): swapped in pairs as the load says */
static void put_file(cave_rom_set *set, const cave_rom_load *load, const uint8_t *chip)
{
	const uint32_t size = files[load->file].size;
	uint8_t **const target = region_of(set, load->region);

	if (!target)
	{
		for (uint32_t word = 0; word < 64u && 2u * word + 1u < size; word++)
			set->eeprom[word] = (uint16_t)(chip[2 * word] << 8 | chip[2 * word + 1]);    /* (16-bit, big endian) */
		return;
	}
	if (load->swapped)    /* ROM_LOAD16_WORD_SWAP: each 16-bit word's bytes the other way round */
		for (uint32_t i = 0; i + 1u < size; i += 2u)
		{
			(*target)[load->offset + i] = chip[i + 1u];
			(*target)[load->offset + i + 1u] = chip[i];
		}
	else
		memcpy(*target + load->offset, chip, size);
}

/* the description's loads checked: each file inside its region */
static bool loads_fit(void)
{
	for (int index = 0; index < ROMS->load_count; index++)
	{
		const cave_rom_load *const load = &ROMS->loads[index];

		if (load->file >= files_count || load->region >= CAVE_ROM_REGIONS
				|| load->offset + files[load->file].size > ROMS->region_size[load->region])
			return false;
	}
	return true;
}

/* the folder part of 'path' (up to its last slash), or none */
static int folder_of(const char *path)
{
	const char *slash = strrchr(path, '/'), *const back = strrchr(path, '\\');

	if (back && (!slash || back > slash))
		slash = back;
	return slash ? (int)(slash - path) + 1 : 0;
}

const char *rom_set_load(const char *source)
{
	return rom_set_load_version(source, 0);
}

const char *rom_set_load_version(const char *source, int version)
{
	static char problem[256];
	uint8_t *chips[ROM_SET_FILES_MOST] = { 0 };
	size_t size = 0;
	uint8_t *whole;
	char path[1024];
	cave_rom_set set;
	int parent;

	if (!use_version(version))
		return "no such version of the game";
	if (!loads_fit())
		return "the game's description puts a file outside its region";
	parent = ROMS->versions[version].parent;
	memset(file_state, 0, sizeof(file_state));
	memset(name_seen, 0, sizeof(name_seen));
	if ((whole = read_whole(source, &size)) != NULL)
	{
		if (!read_zip(whole, size, chips))
		{
			free(whole);
			snprintf(problem, sizeof(problem), "%s is no .zip", source);
			return problem;
		}
		free(whole);
		if (parent >= 0 && !all_found(chips))
		{
			/* (a clone's .zip: the rest from its parent's beside it) */
			snprintf(path, sizeof(path), "%.*s%s.zip", folder_of(source), source, ROMS->versions[parent].mame_name);
			read_zip_file(path, chips);
		}
	}
	else
	{
		read_folder(source, chips);
		if (!all_found(chips))
		{
			snprintf(path, sizeof(path), "%s/%s.zip", source, ROMS->versions[version].mame_name);
			read_zip_file(path, chips);
		}
		if (parent >= 0 && !all_found(chips))
		{
			snprintf(path, sizeof(path), "%s/%s.zip", source, ROMS->versions[parent].mame_name);
			read_zip_file(path, chips);
		}
	}
	for (int file = 0; file < files_count; file++)
		file_state[file] = chips[file] ? FILE_FOUND : name_seen[file] ? FILE_DIFFERENT : FILE_MISSING;
	for (int file = 0; file < files_count; file++)
		if (!chips[file])
		{
			snprintf(problem, sizeof(problem), "the ROM set in %s has no %s (CRC32 %08x), or a different one",
				source, files[file].name, (unsigned)files[file].crc);
			free_chips(chips);
			return problem;
		}

	memset(&set, 0, sizeof(set));
	set.version = version;
	for (int region = 0; region < CAVE_ROM_REGIONS; region++)
		if (region_of(&set, region) && ROMS->region_size[region]
				&& !(*region_of(&set, region) = calloc(1, ROMS->region_size[region])))
		{
			free_set(&set);
			free_chips(chips);
			return "no memory for the ROM set";
		}
	for (int index = 0; index < ROMS->load_count; index++)
		put_file(&set, &ROMS->loads[index], chips[ROMS->loads[index].file]);
	free_chips(chips);
	rom_set_free();    /* (the set loaded before: this one in its place) */
	rom_set = set;
	return NULL;
}

const char *rom_set_load_clone_over(const char *source, int version)
{
	static char problem[256];
	uint8_t *chips[ROM_SET_FILES_MOST] = { 0 };
	bool own[ROM_SET_FILES_MOST];
	const cave_rom_file *parent_files;
	size_t size = 0;
	uint8_t *whole;
	int parent, missing = -1;

	if (!use_version(version) || (parent = ROMS->versions[version].parent) < 0)
		return "no such clone of the game";
	if (!loads_fit())
		return "the game's description puts a file outside its region";
	if (!rom_set.program || rom_set.version != parent)
	{
		snprintf(problem, sizeof(problem), "the %s set's data is needed first (the %s set shares its other chips)",
			ROMS->versions[parent].screen_name, ROMS->versions[version].screen_name);
		return problem;
	}
	parent_files = ROMS->versions[parent].files;
	for (int file = 0; file < files_count; file++)
		own[file] = files[file].crc != parent_files[file].crc || files[file].size != parent_files[file].size;
	memset(file_state, 0, sizeof(file_state));
	memset(name_seen, 0, sizeof(name_seen));
	if ((whole = read_whole(source, &size)) != NULL)
	{
		const int is_zip = read_zip(whole, size, chips);

		free(whole);
		if (!is_zip)
		{
			snprintf(problem, sizeof(problem), "%s is no .zip", source);
			return problem;
		}
	}
	else
	{
		char path[1024];

		read_folder(source, chips);
		for (int file = 0; file < files_count; file++)
			if (own[file] && !chips[file])
			{
				snprintf(path, sizeof(path), "%s/%s.zip", source, ROMS->versions[version].mame_name);
				read_zip_file(path, chips);
				break;
			}
	}
	/* (only the clone's own files count: the others are the parent's, already here) */
	for (int file = 0; file < files_count; file++)
	{
		file_state[file] = !own[file] || chips[file] ? FILE_FOUND : name_seen[file] ? FILE_DIFFERENT : FILE_MISSING;
		if (!own[file])
		{
			free(chips[file]);
			chips[file] = NULL;
		}
		else if (!chips[file] && missing < 0)
			missing = file;
	}
	if (missing >= 0)
	{
		snprintf(problem, sizeof(problem), "%s has no %s (CRC32 %08x), or a different one", source,
			files[missing].name, (unsigned)files[missing].crc);
		free_chips(chips);
		return problem;
	}
	for (int index = 0; index < ROMS->load_count; index++)
		if (own[ROMS->loads[index].file])
			put_file(&rom_set, &ROMS->loads[index], chips[ROMS->loads[index].file]);
	free_chips(chips);
	rom_set.version = version;
	return NULL;
}

const char *rom_set_load_japan_over(const char *source)
{
	return rom_set_load_clone_over(source, DONPACHI_JAPAN);
}

void rom_set_free(void)
{
	free_set(&rom_set);
}

/* ---- the extracted data: a header, then the regions one after the other (those the game has, in
   cave_rom_region's order; the EEPROM's words big endian) ----
   0  the description's data_magic, then zero bytes to 16
   16 the format (1), little endian 32-bit; the version (0: the description's first, MAME's parent set); the size of
      the regions; their CRC32 */

#define DATA_HEADER     32u
#define DATA_FORMAT     1u

static uint32_t data_size(void)
{
	uint32_t size = 0;

	for (int region = 0; region < CAVE_ROM_REGIONS; region++)
		size += ROMS->region_size[region];
	return size;
}

static void put_little32(uint8_t *at, uint32_t value)
{
	for (int byte = 0; byte < 4; byte++)
		at[byte] = (uint8_t)(value >> (8 * byte));
}

const char *rom_set_save_data(const char *path)
{
	static char problem[1100];
	uint32_t size;
	uint8_t *whole;
	uint8_t *at;
	char partial[1040];
	FILE *output;
	size_t written;

	if (!rom_set.program || !ROMS)
		return "no ROM set read";
	size = data_size();
	if (!(whole = malloc(DATA_HEADER + size)))
		return "no memory for the data";
	memset(whole, 0, DATA_HEADER);
	strncpy((char *)whole, ROMS->data_magic, 15);
	at = whole + DATA_HEADER;
	for (int region = 0; region < CAVE_ROM_REGIONS; region++)
	{
		uint8_t **const source = region_of(&rom_set, region);

		if (!ROMS->region_size[region])
			continue;
		if (source)
			memcpy(at, *source, ROMS->region_size[region]);
		else
			for (uint32_t word = 0; word < 64u && 2u * word + 1u < ROMS->region_size[region]; word++)
			{
				at[2 * word] = (uint8_t)(rom_set.eeprom[word] >> 8);
				at[2 * word + 1] = (uint8_t)rom_set.eeprom[word];
			}
		at += ROMS->region_size[region];
	}
	put_little32(whole + 16, DATA_FORMAT);
	put_little32(whole + 20, (uint32_t)rom_set.version);
	put_little32(whole + 24, size);
	put_little32(whole + 28, crc32_of(whole + DATA_HEADER, size));
	/* written beside it first, then put in its place: a program stopped half way leaves no damaged file */
	snprintf(partial, sizeof(partial), "%s.part", path);
	output = fopen(partial, "wb");
	if (!output)
	{
		free(whole);
		snprintf(problem, sizeof(problem), "cannot write %s", partial);
		return problem;
	}
	written = fwrite(whole, 1, DATA_HEADER + size, output);
	free(whole);
	if (fclose(output) != 0 || written != DATA_HEADER + size)
	{
		remove(partial);
		snprintf(problem, sizeof(problem), "cannot write %s (is the disk full?)", partial);
		return problem;
	}
	remove(path);    /* (Windows' rename will not replace a file) */
	if (rename(partial, path) != 0)
	{
		remove(partial);
		snprintf(problem, sizeof(problem), "cannot write %s", path);
		return problem;
	}
	return NULL;
}

const char *rom_set_load_data(const char *path)
{
	return rom_set_load_data_version(path, 0);
}

const char *rom_set_load_data_version(const char *path, int version)
{
	static char problem[1100];
	size_t length = 0;
	uint8_t *whole;
	uint32_t size;
	char magic[16] = { 0 };
	cave_rom_set set;
	const uint8_t *at;

	if (!ROMS || version < 0 || version >= ROMS->version_count)
		return "no such version of the game";
	size = data_size();
	strncpy(magic, ROMS->data_magic, 15);
	if (!(whole = read_whole(path, &length)))
	{
		snprintf(problem, sizeof(problem), "no %s", path);
		return problem;
	}
	if (length != DATA_HEADER + size || memcmp(whole, magic, 16) != 0
			|| little32(whole + 16) != DATA_FORMAT || little32(whole + 20) != (uint32_t)version
			|| little32(whole + 24) != size || little32(whole + 28) != crc32_of(whole + DATA_HEADER, size))
	{
		free(whole);
		snprintf(problem, sizeof(problem), "%s is damaged, or from another version", path);
		return problem;
	}
	memset(&set, 0, sizeof(set));
	set.version = version;
	at = whole + DATA_HEADER;
	for (int region = 0; region < CAVE_ROM_REGIONS; region++)
	{
		uint8_t **const target = region_of(&set, region);

		if (!ROMS->region_size[region])
			continue;
		if (!target)
			for (uint32_t word = 0; word < 64u && 2u * word + 1u < ROMS->region_size[region]; word++)
				set.eeprom[word] = (uint16_t)(at[2 * word] << 8 | at[2 * word + 1]);
		else if ((*target = malloc(ROMS->region_size[region])) != NULL)
			memcpy(*target, at, ROMS->region_size[region]);
		else
		{
			free_set(&set);
			free(whole);
			return "no memory for the data";
		}
		at += ROMS->region_size[region];
	}
	free(whole);
	rom_set_free();
	rom_set = set;
	return NULL;
}
