/**
 *    Name:        sound.h
 *    Description: the boards' sound. DonPachi's: two OKI M6295 ADPCM chips (the music on #0, the effects on #1)
 *                 behind an NMK112, which banks the sample ROMs in 64 KB pages; the 68000 drives all three itself.
 *                 DoDonPachi's: a YMZ280B (8 voices of ADPCM or PCM, 4 MB of samples), whose interrupt tells the
 *                 68000 a voice has ended
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/devices/sound/okim6295.cpp (BSD-3-Clause, Mirko Buffoni, Aaron Giles),
 *    src/devices/sound/okiadpcm.cpp (BSD-3-Clause, Andrew Gardner, Aaron Giles), src/devices/machine/nmk112.cpp
 *    (BSD-3-Clause, Alex W. Jackson) and the donpachi machine of src/mame/cave/cave.cpp (BSD-3-Clause, Luca Elia
 *    and others): #0 at 4.220 MHz / 4, #1 at 4.220 MHz / 2, both with pin 7 high (a sample every 132 clocks), #0
 *    mixed at 1.60 and #1 at 1.0; the NMK112 pages #1's sample table, not #0's.
 *
 *    The chips run as the 68000's time goes: given its clock (cave_sound_clock: the port's program counts its cycles),
 *    the chips are brought up to the moment before each command, bank change or status read, as MAME's stream
 *    update does, so a command takes effect where it falls in the frame; cave_vblank_start() runs them to the
 *    frame's end. Without the clock (tools/cref) all of the frame's samples are made at its end, so the commands take
 *    effect from its start (up to a frame early: the port's way before 0.7.0). The status the program reads (only for
 *    the test mode's sound test, and as it waits for its stop command at the start) is the chips' at that moment.
 *
 *    The output as MAME makes it by default (src/emu/sound.cpp, resampler.cpp, audio_effects/filter.cpp): each
 *    chip's samples to 48 kHz by its "LoFi" resampler (a 4-point interpolation, after Yamaha's), mixed at their
 *    gains, through the speaker's high-pass filter at 20 Hz, then to 16 bits.
 *
 *    The YMZ280B after MAME's src/devices/sound/ymz280b.cpp (BSD-3-Clause, Aaron Giles) and the ddonpach machine of
 *    cave.cpp: 16.9344 MHz, its samples at 88,200 Hz (both outputs to the mono speaker at 1.0), its interrupt merged
 *    with the VBLANK's on the 68000's level 1. It runs by the 68000's clock (cave_sound_run_to), and a voice's end
 *    interrupts at its own sample: DoDonPachi's interrupt handler tells the two apart by the time (the cause's bit 2,
 *    low for 2000 us from the VBLANK: cave/machine.h), so when the chip's interrupt comes is part of the game's timing
 *    and its slowdown, and the decompiled program and MAME's 68000 (tools/cref) take it at the same cycle.
 */
#ifndef CAVE_SOUND_H
#define CAVE_SOUND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAVE_SOUND_RATE     48000u    /* what the ports play: 16-bit mono */

typedef struct
{
	bool playing;
	uint32_t base;        /* the sample's start in the chip's 256 KB */
	uint32_t sample;      /* the nibble it is at */
	uint32_t count;       /* its nibbles */
	int volume;           /* 0..32 (MAME's table, in 32nds) */
	int signal, step;     /* the ADPCM decoder */
} cave_oki_voice;

#define CAVE_SOUND_FRAME_MOST 320u    /* a frame's samples of one chip, at most (#1 makes 278) */

typedef struct
{
	cave_oki_voice voice[4];
	int command;          /* the sample of a play command waiting for its second byte, or -1 */
	uint64_t clocks;      /* (what is left over of the chip's clocks from the last frame, for its next sample) */
	uint32_t due;         /* the frame's samples */
	uint32_t made;        /* those made so far */
	int32_t samples[CAVE_SOUND_FRAME_MOST];
} cave_oki;

/* A YMZ280B's voice: MAME's YMZ280BVoice (its timer left out: a voice ends at the chip's sample that reads its stop) */
typedef struct
{
	uint8_t playing, keyon, looping, mode;
	uint16_t fnum;
	uint8_t level, pan;
	uint32_t start, stop, loop_start, loop_end;    /* in nibbles */
	uint32_t position;
	int32_t signal, step, loop_signal, loop_step;
	uint32_t loop_count;
	int32_t output_left, output_right, output_step, output_pos;
	int16_t last_sample, curr_sample;
} cave_ymz_voice;

typedef struct
{
	cave_ymz_voice voice[8];
	uint8_t current_register, status, irq_state, irq_mask, irq_enable, keyon_enable, ext_mem_enable, ext_readlatch;
	uint32_t ext_mem_address, ext_mem_address_hi, ext_mem_address_mid;
	uint64_t samples;     /* the chip's samples (88,200 a second) made since the board's reset */
	uint64_t check;       /* the first of its samples a voice whose end interrupts can end at (UINT64_MAX: none) */
	uint64_t outputs;     /* the output's samples made of them since the reset */
} cave_ymz;

typedef struct
{
	cave_oki oki[2];
	uint8_t bank[8];      /* the NMK112: chip #0's 4 banks of 64 KB, then #1's */
	uint8_t ymz_register; /* a YMZ280B board's: the register the program last chose, and the last value it wrote to
	                         each (for the test tools) */
	uint8_t ymz[256];
	cave_ymz ymz280b;     /* a YMZ280B board's chip */
	bool frame_begun;     /* (the frame's samples counted: 'due') */
	uint64_t frame_began; /* the 68000's time as the frame began (cave_sound_clock's cycles) */
} cave_sound;

/* The program's bus: the M6295s' status and commands (chip 0 or 1), the NMK112's bank registers (0-7) */
uint8_t cave_sound_status(int chip);
void cave_sound_command(int chip, uint8_t data);
void cave_sound_bank(int which, uint8_t data);

/* A YMZ280B board's chip on the bus (its byte at 300001 and 300003 on DoDonPachi's): a read of 'port' 1 is its status
   (bit n: voice n ended), which the read clears with its interrupt; of port 0, the sample ROM's byte it was set to
   read back. A write to port 0 chooses the register, to port 1 writes it. Both bring the chip up to the 68000's time
   first (cave_access_clock, cave/machine.h), as MAME's stream update does */
uint8_t cave_sound_ymz_read(int port);
void cave_sound_ymz_write(int port, uint8_t data);

/* A YMZ280B board's time. The chip makes its samples by the 68000's clock: sample n at 68000 cycle n * 80000 / 441
   from the board's reset (16 MHz / (16.9344 MHz / 384 * 2)). A voice that comes to its stop address ends at the
   sample that reads it: its bit in the status, and the chip's interrupt (level 1, with the VBLANK's) if the program
   lets it through, as the board's chip does at that moment (MAME finds it at its next stream update instead, up to a
   frame late). cave_sound_event_at: the 68000's cycle by which the chip's interrupt may next come (UINT64_MAX: none
   can); cave_sound_run_to: the chip brought up to the 68000's cycle 'cycles'. The program's runtime (or the reference
   tools) calls it at that cycle, and has the interrupt taken then. cave_sound_events_changed, when set, is called when
   a write or a read changes when the next may come, or the interrupt's line: the runtime looks again */
uint64_t cave_sound_event_at(void);
void cave_sound_run_to(uint64_t cycles);

/* Whether the chip's interrupt is up by the 68000's cycle 'cycles', the chip left where it is (a copy of it run
   there). For MAME's 68000 (tools/cref), stopped at the event's cycle part way into an instruction: the line goes up
   there, and the chip is brought there as the next instruction begins, after that instruction's accesses, as the
   decompiled program has them (the event at the instruction's end) */
bool cave_sound_irq_at(uint64_t cycles);
extern void (*cave_sound_events_changed)(void);


/* The chips after the board's reset: silent, no command waiting */
void cave_sound_reset(void);

/* The chips to the end of the board's frame (cave_vblank_start() calls it): the frame's samples go to the output */
void cave_sound_frame(void);

/* The 68000's time, for where the program's commands fall in a frame: 'cycles_now' gives its cycles so far, a frame
   being 'frame_cycles' of them. NULL (as it starts): every command from its frame's start */
void cave_sound_clock(uint64_t (*cycles_now)(void), uint64_t frame_cycles);

/* While set, cave_sound_frame makes nothing and leaves the output's own state (its resampler's and filter's history,
   kept outside the board) as it is: for a frame the port runs and then throws away, with a save state put back after
   it (the look-ahead picture: one frame run on to draw the picture its vblank interrupt sets, then back). The chips'
   own state is the board's, put back with the save state */
extern bool cave_sound_quiet;

/* The samples made so far and not yet taken, at most 'most' of them into 'out': how many. The output keeps about
   0.7 s; what a port does not take in time is dropped, the oldest first */
unsigned cave_sound_take(int16_t *out, unsigned most);

/* Drops the samples not yet taken (a port that skips ahead: its fast forward) */
void cave_sound_drop(void);

#ifdef __cplusplus
}
#endif

#endif
