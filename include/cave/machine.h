/**
 *    Name:        machine.h
 *    Description: DonPachi's board (Cave, AT-C01DP-2) as the decompiled program sees it: its memory map, the
 *                 interrupt, the inputs and the EEPROM
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/cave/cave.cpp (donpachi_map, irq_cause_r, eeprom_w, INPUT_PORTS_START( cave );
 *    BSD-3-Clause, Luca Elia and others) and src/devices/video/tmap038.cpp (the NEC 038 tilemaps).
 *    Shaped like tmntarcade-decomp's runtime/include/tmnt/machine.h (BSD-3-Clause, Sebastian Toyos).
 *
 *    One 68000 at 16 MHz and no sound CPU: the 68000 drives the two OKI M6295s and the NMK112 itself.
 *      000000-07FFFF  program ROM          100000-10FFFF  work RAM
 *      200000-207FFF  layer 1 (038)        300000-307FFF  layer 0 (038)    400000-403FFF  layer 2, 8x8 only
 *      500000-50FFFF  sprite RAM           600000/700000/800000  layers 1, 0, 2: their 3 registers
 *      900000-90007F  video registers (900000-900007 read: the interrupt's cause)
 *      A08000-A08FFF  palette (xGRB 555)   B00000/B00010  M6295 #0, #1    B00020-B0002F  NMK112 banks
 *      C00000  inputs 1     C00002  inputs 2 + the EEPROM's data out (bit 11)     D00000  EEPROM lines
 */
#ifndef CAVE_MACHINE_H
#define CAVE_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

#include "cave/sound.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAVE_CPU_CLOCK        16000000u
#define CAVE_FRAME_USEC       17376u        /* MAME: 17376 us a frame (57.55 Hz) */

#define CAVE_PROGRAM_SIZE     0x080000u
#define CAVE_WORK_RAM         0x100000u
#define CAVE_WORK_RAM_SIZE    0x10000u
#define CAVE_LAYER_RAM_WORDS  0x4000u       /* 32 KB a layer (tmap038: 16x16 RAM, line RAM, 8x8 RAM) */
#define CAVE_SPRITE_RAM_WORDS 0x8000u       /* 64 KB */
#define CAVE_PALETTE_WORDS    0x8000u       /* the most a board has (DoDonPachi's 64 KB; DonPachi's is 0x800) */
#define CAVE_VIDEO_REG_WORDS  0x40u
#define CAVE_EEPROM_WORDS     64u           /* 93C46, 16-bit words */
#define CAVE_STACK_FLOOR      0x10F800u     /* the 68000's stack, down from 110000 (reset's SP): the decompiled
                                               program keeps its own in C, so the checks leave these 2 KB out */

/* The inputs, MAME's IN0 and IN1: active low */
enum
{
	CAVE_UP = 0x0001, CAVE_DOWN = 0x0002, CAVE_LEFT = 0x0004, CAVE_RIGHT = 0x0008,
	CAVE_BUTTON1 = 0x0010, CAVE_BUTTON2 = 0x0020, CAVE_BUTTON3 = 0x0040, CAVE_START = 0x0080,
	CAVE_COIN = 0x0100,
	CAVE_SERVICE = 0x0200,          /* IN0: the test mode; IN1: the service coin */
	CAVE_EEPROM_DO = 0x0800         /* IN1, active high: the EEPROM's data out */
};

typedef struct
{
	/* the EEPROM's state: its lines, and the bits of the command or data being shifted */
	uint16_t words[CAVE_EEPROM_WORDS];
	bool cs, clk, di, out;
	bool write_enabled;
	uint32_t shift;
	int bits;
	int state;
	int address;
	int pending_op;             /* the write or erase CS's fall carries out: 1 WRITE, 3 ERASE, 4 ERAL, 5 WRAL */
	uint16_t data;              /* the word being read out */
} cave_eeprom;

typedef struct
{
	uint8_t work_ram[CAVE_WORK_RAM_SIZE];    /* the other way round: cave_work_ram_at */
	uint16_t layer_ram[3][CAVE_LAYER_RAM_WORDS];
	uint16_t layer_regs[3][3];
	uint16_t sprite_ram[CAVE_SPRITE_RAM_WORDS];
	uint16_t video_regs[CAVE_VIDEO_REG_WORDS];
	uint16_t palette[CAVE_PALETTE_WORDS];
	bool vblank_irq;                          /* raised when VBLANK starts, cleared by reading 900004 */
	bool unknown_irq;
	uint64_t vblank_began;                    /* the 68000's time (cave_clock's cycles) as the last VBLANK began (the
	                                             program's runtime sets the VBLANK's own cycle after cave_vblank_start) */
	cave_sound sound;                         /* the M6295s and the NMK112 (cave/sound.h) */
	uint8_t eeprom_lines;                     /* the last byte written to D00000 */
	cave_eeprom eeprom;
	uint16_t in0, in1;                        /* active low, as the port sets them */
	/* the ROM set's regions, big endian as the 68000 sees them (rom_set.c) */
	const uint8_t *program;
	uint64_t frame;                           /* VBLANKs since reset */
	uint32_t unmapped_reads, unmapped_writes;
	uint32_t last_unmapped_read, last_unmapped_write;    /* (their addresses, for the checks) */
} cave_machine;

extern cave_machine cave_board;

/* The host's place of the 'size' bytes of work RAM from 'address' (a big-endian value there is a little-endian
   one at this place) */
static inline void *cave_work_ram_at(uint32_t address, uint32_t size)
{
	return &cave_board.work_ram[CAVE_WORK_RAM_SIZE - size - ((address - CAVE_WORK_RAM) & (CAVE_WORK_RAM_SIZE - 1u))];
}

static inline uint8_t *cave_work_ram_byte(uint32_t address)
{
	return (uint8_t *)cave_work_ram_at(address, 1u);
}

/* The board after its reset: RAMs cleared, the inputs idle, the EEPROM as the ROM set has it */
void cave_reset(void);

uint16_t cave_read16(uint32_t address, uint16_t mem_mask);
void cave_write16(uint32_t address, uint16_t data, uint16_t mem_mask);

/* The interrupt level the 68000 sees: 1 while an interrupt is pending (the VBLANK's, or a YMZ280B board's sound chip's:
   cave/sound.h), else 0 */
int cave_irq_level(void);

/* The 68000's time in cycles, from the port's program (cave_sound_clock sets it): NULL until then */
extern uint64_t (*cave_clock)(void);

/* The 68000's time as the instruction doing a bus access began (NULL: cave_clock), for what the board answers by the
   time: the interrupt cause's bit 2 and a YMZ280B's samples. The decompiled program has the time of an instruction's
   start only (its cycles counted at its end); MAME's 68000 (tools/cref) is part way into the instruction by its
   access, and gives the instruction's start here, so that both see the board alike (MAME's own board reads its
   timers at the access: a few cycles later) */
extern uint64_t (*cave_access_clock)(void);

/* The cycle the next VBLANK's interrupt comes at, while the program's C runs an instruction (NULL, or UINT64_MAX:
   none known). An instruction that begins at that cycle or after, before the runtime has had the VBLANK (it has it
   as the instruction's point passes), reads the interrupt's cause as the board answers then: the VBLANK begun (ESP
   Ra.De.'s boot polls bit 2, "btst #2, $800001; bne", and one of its reads begins on the VBLANK's own cycle) */
extern uint64_t (*cave_vblank_due)(void);

/* Called, when set, at each write to the board's devices (all but the work RAM), for the test tools: the writes of
   the port and of MAME's 68000 (tools/cref) set side by side, each with its cycle */
extern void (*cave_write_seen)(uint32_t address, uint16_t data, uint16_t mem_mask);

/* Called, when set, as the program reads the interrupt's cause (900000-900007, 'word' 0-3) or an OKI M6295's status
   (4, 5: #0, #1), with what the board answered: the test tools set the port's reads beside MAME's
   (tests/harness/mame_check, dp_check.lua's irq.txt) */
extern void (*cave_irq_cause_read)(uint32_t word, uint16_t value);

/* The screen's VBLANK begins: the interrupt, and the frame's sound (cave_sound_frame) */
void cave_vblank_start(void);

/* The EEPROM's 64 words to and from a file: 128 bytes, each word little endian, as MAME's nvram/donpachi/eeprom
   (eeprom.cpp; the ROM set's eeprom-donpachi.u10 is big endian). cave_eeprom_load reads the port's older files too
   (big endian, before 0.7.0): whichever order is nearer the ROM set's. 1 loaded, 0 no such file (the EEPROM stays as
   it is), -1 a file of another size (left alone). cave_eeprom_save: NULL, or what went wrong */
int cave_eeprom_load(const char *path);
const char *cave_eeprom_save(const char *path);

/* A hash of what the program has made of the board's memories (work RAM, layers and their registers, sprites,
   palette, video registers), for comparing the decompiled program with the original on MAME's 68000 (tools/cref) */
uint64_t cave_state_hash(void);

/* The board's memories, one region after the other, for finding where two runs differ: NULL, or what is wrong */
const char *cave_dump(const char *path);

#ifdef __cplusplus
}
#endif

#endif
