/**
 *    Name:        machine.c
 *    Description: the board: the 68000's bus (as the game's description has it: cave/board.h), the interrupt and
 *                 its cause, the inputs and the 93C46 EEPROM
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/cave/cave.cpp (BSD-3-Clause): its memory maps (the games' in src/boards/), videoregs_r, irq_cause_r,
 *    update_irq_state, eeprom_w; the EEPROM after src/devices/machine/eepromser.cpp (93C46, 64 words of 16 bits).
 *    The sound chips: cave/sound.h (sound.c). B00000-B00003, B00010-B00013 and B00020-B0002F take the word's low byte
 *    (MAME's umask16(0x00ff)).
 */
#include "cave/machine.h"
#include "cave/boards.h"
#include "cave/rom_set.h"

#include <stdio.h>
#include <string.h>

cave_machine cave_board;

/* ---- the EEPROM: a 93C46 in its 16-bit mode, its lines from D00000 (bit 3 DI, bit 1 CS, bit 2 CLK) ---- */

enum
{
	EEPROM_IDLE,          /* CS low, or the command done */
	EEPROM_START,         /* waiting for the start bit */
	EEPROM_COMMAND,       /* the opcode's 2 bits and the address's 6 */
	EEPROM_READ,          /* shifting a word out */
	EEPROM_WRITE_DATA,    /* shifting a word in (WRITE, WRAL) */
	EEPROM_DONE           /* a write or erase waiting for CS to fall */
};

enum { OP_READ = 2, OP_WRITE = 1, OP_ERASE = 3, OP_EXTENDED = 0 };


static void eeprom_clock_rising(cave_eeprom *e)
{
	switch (e->state)
	{
	case EEPROM_START:
		if (e->di)
		{
			e->state = EEPROM_COMMAND;
			e->shift = 0;
			e->bits = 0;
		}
		break;
	case EEPROM_COMMAND:
		e->shift = e->shift << 1 | (e->di ? 1u : 0u);
		if (++e->bits == 8)
		{
			const int op = (int)(e->shift >> 6 & 3u);

			e->address = (int)(e->shift & 0x3Fu);
			e->bits = 0;
			e->shift = 0;
			if (op == OP_READ)
			{
				e->state = EEPROM_READ;
				e->out = false;          /* the dummy 0 before the data */
				e->data = e->words[e->address];
			}
			else if (op == OP_WRITE)
			{
				e->state = EEPROM_WRITE_DATA;
				e->pending_op = 1;
			}
			else if (op == OP_ERASE)
			{
				e->state = EEPROM_DONE;
				e->pending_op = 3;
			}
			else
				switch (e->address >> 4)
				{
				case 3: e->write_enabled = true; e->state = EEPROM_IDLE; break;     /* EWEN */
				case 0: e->write_enabled = false; e->state = EEPROM_IDLE; break;    /* EWDS */
				case 2: e->state = EEPROM_DONE; e->pending_op = 4; break;       /* ERAL */
				default: e->state = EEPROM_WRITE_DATA; e->pending_op = 5; break; /* WRAL */
				}
		}
		break;
	case EEPROM_READ:
		e->out = (e->data & 0x8000u) != 0;
		e->data = (uint16_t)(e->data << 1);
		if (++e->bits == 16)
		{
			/* (a longer read goes on with the next word) */
			e->bits = 0;
			e->address = (e->address + 1) & (int)(CAVE_EEPROM_WORDS - 1u);
			e->data = e->words[e->address];
		}
		break;
	case EEPROM_WRITE_DATA:
		e->shift = e->shift << 1 | (e->di ? 1u : 0u);
		if (++e->bits == 16)
			e->state = EEPROM_DONE;
		break;
	default:
		break;
	}
}

static void eeprom_select_falling(cave_eeprom *e)
{
	if (e->state == EEPROM_DONE && e->write_enabled)
		switch (e->pending_op)
		{
		case 1: e->words[e->address] = (uint16_t)e->shift; break;
		case 3: e->words[e->address] = 0xFFFFu; break;
		case 4: for (int i = 0; i < (int)CAVE_EEPROM_WORDS; i++) e->words[i] = 0xFFFFu; break;
		case 5: for (int i = 0; i < (int)CAVE_EEPROM_WORDS; i++) e->words[i] = (uint16_t)e->shift; break;
		default: break;
		}
	e->pending_op = 0;
	e->state = EEPROM_IDLE;
	e->out = true;    /* ready */
}

static void eeprom_write_lines(uint8_t data)
{
	cave_eeprom *const e = &cave_board.eeprom;
	const bool di = (data & 0x08u) != 0, cs = (data & 0x02u) != 0, clk = (data & 0x04u) != 0;

	e->di = di;
	if (cs && !e->cs)
	{
		e->state = EEPROM_START;
		e->bits = 0;
	}
	else if (!cs && e->cs)
		eeprom_select_falling(e);
	e->cs = cs;
	if (cs && clk && !e->clk)
		eeprom_clock_rising(e);
	e->clk = clk;
}

/* ---- the interrupt ---- */

int cave_irq_level(void)
{
	return (cave_board.vblank_irq || cave_board.unknown_irq || cave_board.sound.ymz280b.irq_state) ? 1 : 0;
}

uint64_t (*cave_clock)(void);
uint64_t (*cave_access_clock)(void);

void cave_vblank_start(void)
{
	cave_board.vblank_irq = true;
	cave_board.vblank_began = cave_clock ? cave_clock() : 0u;
	cave_board.frame++;
	cave_sound_frame();
}

/* 900000-900007: the cause, bit 0 low for VBLANK, bit 1 low for the other; reading 900004 clears VBLANK's,
   900006 the other's (irq_cause_r). Word 0's bit 2 is low for the first 2000 us of each VBLANK (MAME's
   m_agallet_vblank_irq: set by vblank_start, cleared by vblank_end 2000 us later), high after: DoDonPachi's interrupt
   handler reads it after clearing VBLANK's cause (800004, then 800000) and takes the frame's work only while it is
   low (high: the sound chip's interrupt; the chip's interrupt in those 2000 us gets the frame's work again). Read at
   the instruction's start (cave_access_clock). DonPachi's handler does not read it */
#define AGALLET_CYCLES (CAVE_CPU_CLOCK / 1000000u * 2000u)    /* 2000 us of the 68000: 32,000 cycles */
void (*cave_irq_cause_read)(uint32_t word, uint16_t value);

static uint16_t irq_cause(uint32_t word)
{
	uint16_t result = 0x0003u;

	if (cave_board.vblank_irq)
		result ^= 0x01u;
	if (cave_board.unknown_irq)
		result ^= 0x02u;
	if (word == 2)
		cave_board.vblank_irq = false;
	if (word == 3)
		cave_board.unknown_irq = false;
	if (word == 0 && !(cave_clock && (cave_access_clock ? cave_access_clock() : cave_clock()) - cave_board.vblank_began
		< AGALLET_CYCLES))
		result |= 4u;
	return result;
}

/* ---- the bus ---- */

static void combine(uint16_t *word, uint16_t data, uint16_t mem_mask)
{
	*word = (uint16_t)((*word & ~mem_mask) | (data & mem_mask));
}

/* ---- the board's description (cave/board.h) ---- */

const cave_board_desc *cave_board_description = &cave_board_donpachi;

/* the regions in each 64 KB page of the bus, in the description's order: the bus looks only through its address's
   page (a few regions at most), as fast as the fixed map it replaces */
#define PAGES       256
#define PAGE_MOST   6
static const cave_region *pages[PAGES][PAGE_MOST];
static uint8_t page_count[PAGES];
static const cave_board_desc *paged;    /* (the description the pages are for) */
/* the program ROM and the work RAM, the regions most reads and writes are for, looked at before the pages */
static uint32_t program_end, work_start, work_end;

/* a page that holds one plain RAM and nothing else (a layer's, the sprites', the palette): its words, straight */
typedef struct
{
	uint16_t *ram;              /* NULL: the page is not one */
	uint32_t start, end, mask;
	uint32_t offset;
} ram_page;
static ram_page ram_pages[PAGES];

static uint16_t *ram_of(const cave_region *r)
{
	switch (r->kind)
	{
	case CAVE_REGION_LAYER_RAM: return cave_board.layer_ram[r->unit];
	case CAVE_REGION_SPRITE_RAM: return cave_board.sprite_ram;
	case CAVE_REGION_PALETTE: return cave_board.palette;
	default: return NULL;
	}
}

static void make_pages(void)
{
	const cave_board_desc *const d = cave_board_description;

	memset(page_count, 0, sizeof(page_count));
	memset(ram_pages, 0, sizeof(ram_pages));
	program_end = work_start = work_end = 0u;
	for (int i = 0; i < d->map_count; i++)
	{
		const cave_region *const r = &d->map[i];

		for (uint32_t page = r->start >> 16; page <= ((r->end - 1u) >> 16) && page < PAGES; page++)
			if (page_count[page] < PAGE_MOST)
				pages[page][page_count[page]++] = r;
		if (r->kind == CAVE_REGION_PROGRAM && r->start == 0u && !program_end)
			program_end = r->end;
		if (r->kind == CAVE_REGION_WORK_RAM && !work_end)
		{
			work_start = r->start;
			work_end = r->end;
		}
	}
	for (int page = 0; page < PAGES; page++)
		if (page_count[page] == 1 && ram_of(pages[page][0]))
		{
			const cave_region *const r = pages[page][0];

			ram_pages[page] = (ram_page){ ram_of(r), r->start, r->end, r->mask ? r->mask : 0xFFFFFFFFu, r->offset };
		}
	paged = d;
}

void cave_board_describe(const cave_board_desc *description)
{
	cave_board_description = description ? description : &cave_board_donpachi;
	make_pages();
}

/* the region of the bus that holds 'address' (reads skip CAVE_REGION_IGNORED's), or NULL */
static const cave_region *region_at(uint32_t address, int writing)
{
	const uint32_t page = address >> 16 & (PAGES - 1u);

	for (int i = 0; i < page_count[page]; i++)
	{
		const cave_region *const r = pages[page][i];

		if (address >= r->start && address < r->end && (writing || r->kind != CAVE_REGION_IGNORED))
			return r;
	}
	return NULL;
}

/* a region's word for 'address': its offset, the address's bits it uses (all of its size, or its mask's) */
static uint32_t word_in(const cave_region *r, uint32_t address)
{
	const uint32_t from = address - r->start;

	return r->offset + ((r->mask ? from & r->mask : from) >> 1);
}

static uint16_t unmapped_read(uint32_t address)
{
	cave_board.unmapped_reads++;
	cave_board.last_unmapped_read = address;
	return 0u;    /* (as MAME's: the address space's unmapped value, 0 for these boards) */
}

static void unmapped_write(uint32_t address)
{
	cave_board.unmapped_writes++;    /* (the program ROM, and what no device answers) */
	cave_board.last_unmapped_write = address;
}

uint16_t cave_read16(uint32_t address, uint16_t mem_mask)
{
	const cave_region *r;

	(void)mem_mask;
	address &= 0xFFFFFEu;
	if (paged != cave_board_description)
		make_pages();
	if (address < program_end)
		return cave_board.program ? (uint16_t)(cave_board.program[address] << 8 | cave_board.program[address + 1u]) : 0u;
	if (address - work_start < work_end - work_start)
		return *(uint16_t *)cave_work_ram_at(address, 2u);
	{
		const ram_page *const p = &ram_pages[address >> 16 & (PAGES - 1u)];

		if (p->ram && address >= p->start && address < p->end)
			return p->ram[p->offset + (((address - p->start) & p->mask) >> 1)];
	}
	if (!(r = region_at(address, 0)))
		return unmapped_read(address);
	switch (r->kind)
	{
	case CAVE_REGION_PROGRAM:
		return cave_board.program ? (uint16_t)(cave_board.program[address] << 8 | cave_board.program[address + 1u]) : 0u;
	case CAVE_REGION_WORK_RAM:
		return *(uint16_t *)cave_work_ram_at(address, 2u);
	case CAVE_REGION_LAYER_RAM:
		return cave_board.layer_ram[r->unit][word_in(r, address)];
	case CAVE_REGION_LAYER_REGS:
		return cave_board.layer_regs[r->unit][word_in(r, address)];
	case CAVE_REGION_SPRITE_RAM:
		return cave_board.sprite_ram[word_in(r, address)];
	case CAVE_REGION_VIDEO_REGS:
	{
		const uint32_t word = word_in(r, address);

		if (word < 4u)
		{
			const uint16_t value = irq_cause(word);

			if (cave_irq_cause_read)
				cave_irq_cause_read(word, value);
			return value;
		}
		return 0u;
	}
	case CAVE_REGION_PALETTE:
		return cave_board.palette[word_in(r, address)];
	case CAVE_REGION_OKI:
	{
		const uint16_t value = cave_sound_status(r->unit);

		if (cave_irq_cause_read)
			cave_irq_cause_read(4u + r->unit, value);    /* (4, 5: the sound chips' status) */
		return value;
	}
	case CAVE_REGION_YMZ280B:
		return cave_sound_ymz_read((int)(word_in(r, address) & 1u));    /* (the chip's byte: the low one) */
	case CAVE_REGION_INPUTS:
		return r->unit == 0 ? cave_board.in0
			: (uint16_t)((cave_board.in1 & ~CAVE_EEPROM_DO) | (cave_board.eeprom.out ? CAVE_EEPROM_DO : 0u));
	default:
		return unmapped_read(address);    /* (written only: the NMK112, the EEPROM's lines) */
	}
}

void cave_write16(uint32_t address, uint16_t data, uint16_t mem_mask)
{
	const cave_region *r;

	address &= 0xFFFFFEu;
	if (paged != cave_board_description)
		make_pages();
	if (address - work_start < work_end - work_start)
	{
		combine((uint16_t *)cave_work_ram_at(address, 2u), data, mem_mask);
		return;
	}
	{
		const ram_page *const p = &ram_pages[address >> 16 & (PAGES - 1u)];

		if (p->ram && address >= p->start && address < p->end)
		{
			combine(&p->ram[p->offset + (((address - p->start) & p->mask) >> 1)], data, mem_mask);
			return;
		}
	}
	if (!(r = region_at(address, 1)))
	{
		unmapped_write(address);
		return;
	}
	switch (r->kind)
	{
	case CAVE_REGION_WORK_RAM:
		combine((uint16_t *)cave_work_ram_at(address, 2u), data, mem_mask);
		break;
	case CAVE_REGION_LAYER_RAM:
		combine(&cave_board.layer_ram[r->unit][word_in(r, address)], data, mem_mask);
		break;
	case CAVE_REGION_LAYER_REGS:
		combine(&cave_board.layer_regs[r->unit][word_in(r, address)], data, mem_mask);
		break;
	case CAVE_REGION_SPRITE_RAM:
		combine(&cave_board.sprite_ram[word_in(r, address)], data, mem_mask);
		break;
	case CAVE_REGION_IGNORED:
		break;    /* the watchdog (sprite013.cpp: "offset 0x68 or 0x78 is commonly watchdog"): the interrupt writes it
		             whatever D0 holds, a value the decompiled program does not have */
	case CAVE_REGION_VIDEO_REGS:
		combine(&cave_board.video_regs[word_in(r, address)], data, mem_mask);
		break;
	case CAVE_REGION_PALETTE:
		combine(&cave_board.palette[word_in(r, address)], data, mem_mask);
		break;
	case CAVE_REGION_OKI:
		if (mem_mask & 0x00FFu)
			cave_sound_command(r->unit, (uint8_t)data);
		break;
	case CAVE_REGION_YMZ280B:
		if (mem_mask & 0x00FFu)
			cave_sound_ymz_write((int)(word_in(r, address) & 1u), (uint8_t)data);
		break;
	case CAVE_REGION_NMK112:
		if (mem_mask & 0x00FFu)
			cave_sound_bank((int)word_in(r, address), (uint8_t)data);
		break;
	case CAVE_REGION_EEPROM:
	{
		/* (a byte write: its byte; at an even address the word's high half) */
		const uint8_t lines = (mem_mask & 0xFF00u) ? (uint8_t)(data >> 8) : (uint8_t)data;

		cave_board.eeprom_lines = lines;
		eeprom_write_lines(lines);
		break;
	}
	default:
		unmapped_write(address);    /* (the program ROM, the inputs) */
		break;
	}
}

void cave_reset(void)
{
	const uint8_t *const program = cave_board.program;
	uint16_t words[CAVE_EEPROM_WORDS];

	memcpy(words, cave_board.eeprom.words, sizeof(words));
	memset(&cave_board, 0, sizeof(cave_board));
	cave_board.program = program;
	memcpy(cave_board.eeprom.words, words, sizeof(words));
	cave_board.eeprom.out = true;
	cave_board.in0 = 0xFFFFu;
	cave_board.in1 = 0xFFFFu;
	cave_sound_reset();
}

/* ---- the EEPROM's file ---- */

/* how many of the bytes differ from the ROM set's EEPROM, the file's words read as little endian (MAME's) or big
   endian (the port's files before 0.7.0) */
static int eeprom_changed_bytes(const uint8_t *bytes, bool little)
{
	int changed = 0;

	for (int word = 0; word < (int)CAVE_EEPROM_WORDS; word++)
	{
		const uint16_t value = little ? (uint16_t)(bytes[2 * word] | bytes[2 * word + 1] << 8)
			: (uint16_t)(bytes[2 * word] << 8 | bytes[2 * word + 1]);

		changed += (value >> 8) != (rom_set.eeprom[word] >> 8);
		changed += (value & 0xFFu) != (rom_set.eeprom[word] & 0xFFu);
	}
	return changed;
}

int cave_eeprom_load(const char *path)
{
	uint8_t bytes[2 * CAVE_EEPROM_WORDS + 1];
	FILE *file = fopen(path, "rb");
	size_t size;
	bool little;

	if (!file)
		return 0;
	size = fread(bytes, 1, sizeof(bytes), file);
	fclose(file);
	if (size != 2 * CAVE_EEPROM_WORDS)
		return -1;
	/* MAME's order, unless the other is plainly the one: the test mode changes a few of the ROM set's bytes, so read
	   the wrong way round a file has more of them changed (the ROM set's first words, 000C FFFB, are not the same
	   swapped). A tie (both of a word's bytes changed, the same way round): MAME's */
	little = eeprom_changed_bytes(bytes, true) <= eeprom_changed_bytes(bytes, false);
	for (int word = 0; word < (int)CAVE_EEPROM_WORDS; word++)
		cave_board.eeprom.words[word] = little ? (uint16_t)(bytes[2 * word] | bytes[2 * word + 1] << 8)
			: (uint16_t)(bytes[2 * word] << 8 | bytes[2 * word + 1]);
	return 1;
}

const char *cave_eeprom_save(const char *path)
{
	uint8_t bytes[2 * CAVE_EEPROM_WORDS];
	FILE *file;
	bool written;

	for (int word = 0; word < (int)CAVE_EEPROM_WORDS; word++)
	{
		bytes[2 * word] = (uint8_t)cave_board.eeprom.words[word];    /* (little endian: MAME's nvram) */
		bytes[2 * word + 1] = (uint8_t)(cave_board.eeprom.words[word] >> 8);
	}
	if (!(file = fopen(path, "wb")))
		return "cannot write the EEPROM's file";
	written = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
	if (fclose(file) != 0 || !written)
		return "cannot write the EEPROM's file";
	return NULL;
}

/* ---- for the checks ---- */

static uint64_t fnv(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = data;

	for (size_t index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 0x100000001B3ull;
	return hash;
}

/* the board's own palette RAM, in bytes (the array has room for the largest) */
static size_t palette_bytes(void)
{
	const cave_video_desc *const video = cave_board_description->video;

	return video && video->palette_words && video->palette_words <= CAVE_PALETTE_WORDS
		? video->palette_words * sizeof(uint16_t) : sizeof(cave_board.palette);
}

uint64_t cave_state_hash(void)
{
	uint64_t hash = 0xCBF29CE484222325ull;

	for (uint32_t address = CAVE_WORK_RAM; address < CAVE_STACK_FLOOR; address++)
		hash = fnv(hash, cave_work_ram_byte(address), 1u);
	hash = fnv(hash, cave_board.layer_ram, sizeof(cave_board.layer_ram));
	hash = fnv(hash, cave_board.layer_regs, sizeof(cave_board.layer_regs));
	hash = fnv(hash, cave_board.sprite_ram, sizeof(cave_board.sprite_ram));
	hash = fnv(hash, cave_board.video_regs, sizeof(cave_board.video_regs));
	hash = fnv(hash, cave_board.palette, palette_bytes());
	return hash;
}

const char *cave_dump(const char *path)
{
	FILE *output = fopen(path, "wb");

	if (!output)
		return "cannot write the dump";
	/* (the work RAM as the 68000 sees it, from 100000 up) */
	for (uint32_t address = CAVE_WORK_RAM; address < CAVE_WORK_RAM + CAVE_WORK_RAM_SIZE; address++)
		fputc(*cave_work_ram_byte(address), output);
	fwrite(cave_board.layer_ram, 1, sizeof(cave_board.layer_ram), output);
	fwrite(cave_board.layer_regs, 1, sizeof(cave_board.layer_regs), output);
	fwrite(cave_board.sprite_ram, 1, sizeof(cave_board.sprite_ram), output);
	fwrite(cave_board.video_regs, 1, sizeof(cave_board.video_regs), output);
	fwrite(cave_board.palette, 1, palette_bytes(), output);
	fclose(output);
	return NULL;
}
