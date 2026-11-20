/**
 *    Name:        sound.c
 *    Description: the sound (cave/sound.h), as the game's description has it: two OKI M6295s, their ADPCM, the
 *                 NMK112's banks; or a YMZ280B, its voices and its interrupt; and the mix the ports play
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/devices/sound/okim6295.cpp (BSD-3-Clause, Mirko Buffoni, Aaron Giles): write(), read(),
 *    generate_adpcm(), s_volume_table; src/devices/sound/okiadpcm.cpp (BSD-3-Clause, Andrew Gardner, Aaron Giles):
 *    oki_adpcm_state's clock(), reset(), compute_tables(); src/devices/machine/nmk112.cpp (BSD-3-Clause, Alex W.
 *    Jackson): okibank_w() and its banks, with device_generic_cart_interface::map_non_power_of_two()'s way of
 *    folding the entries past the ROM onto it. The output after src/emu/resampler.cpp (BSD-3-Clause, Olivier
 *    Galibert): audio_resampler_lofi, its interpolation table and its delay, src/emu/audio_effects/filter.cpp
 *    (BSD-3-Clause, Olivier Galibert): the high-pass filter, and src/emu/sound.cpp's mix: MAME's defaults.
 *    The YMZ280B after src/devices/sound/ymz280b.cpp (BSD-3-Clause, Aaron Giles): write_to_register(),
 *    compute_status(), read(), update_step(), update_volumes(), update_irq_state(), generate_adpcm(), generate_pcm8(),
 *    generate_pcm16(), sound_stream_update() and device_reset(), made a sample at a time: a voice reads its next sample
 *    as the chip's sample needs it, and its end raises the interrupt then (MAME's makes a stream update's samples at
 *    once, and signals the ends it finds there as the update ends).
 */
#include "cave/sound.h"
#include "cave/board.h"
#include "cave/machine.h"
#include "cave/rom_set.h"

#include <math.h>
#include <string.h>

#define FRAME_USEC      CAVE_FRAME_USEC     /* the board's frame: 17376 us (MAME's timer) */
#define OUTPUT_SIZE     32768u              /* the output's ring: a power of 2, about 0.7 s */
#define HIGH_PASS_HZ    20.0                /* the speaker's filter (audio_effect_filter: on, at 20 Hz, Q 0.7071) */
#define HIGH_PASS_Q     0.7071067f
#define HISTORY         4                   /* the resampler's samples from the frame before */

/* the game's chips (cave/board.h's cave_sound_desc): their clocks, divisor and levels; their ROM regions, kept after
   'skip' empty bytes (the NMK112's old layout: DonPachi's "oki1" atdp.u33, "oki2" atdp.u32 then atdp.u33) */
#define SOUND           (cave_board_description->sound)
#define OKI_DIVISOR     (SOUND->divisor)
#define REGION_SKIP     (SOUND->skip)

/* each chip's 64 KB pages, and the mask over its bank values (the pages rounded up to a power of 2, less 1), for
   the description they were counted for */
static uint32_t chip_pages[2], chip_mask[2];
static uint32_t chip_skip;
static uint8_t chip_paged;
static const void *pages_for;

static void count_pages(void)
{
	for (int chip = 0; chip < 2; chip++)
	{
		const uint32_t pages = (rom_set_region_size(CAVE_ROM_SOUND0 + chip) - REGION_SKIP) >> 16;
		uint32_t mask = pages - 1u;

		while (mask & (mask + 1u))
			mask |= mask >> 1;
		chip_pages[chip] = pages;
		chip_mask[chip] = mask;
	}
	chip_skip = REGION_SKIP;
	chip_paged = SOUND->paged_table;
	pages_for = cave_board_description;
}

/* MAME's s_volume_table, in 32nds: 0, -3.2, -6, -9.2, -12, -14.5, -18, -20.5, -24 dB, then silent */
static const int volume_table[16] = { 0x20, 0x16, 0x10, 0x0b, 0x08, 0x06, 0x04, 0x03, 0x02, 0, 0, 0, 0, 0, 0, 0 };
static const int index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static int diff_lookup[49 * 16];
static bool tables_made;

static int16_t output[OUTPUT_SIZE];
static uint32_t output_read, output_write;     /* (free running: the ring's places are these & (OUTPUT_SIZE - 1)) */
static uint64_t output_clock;                  /* (what is left over of the output's samples from the last frame) */

/* the resampling, per chip: where the next output sample falls among the chip's samples of this frame (MAME's
   LoFi takes the 4 before it: the frame before's last ones, 'history'), and its interpolation table */
static double resample_position[2];
static float history[2][HISTORY];

/* The groups' levels (cave_sound_set_level), and what is heard of each chip: its samples again, each voice at its
   group's level. The chips' own samples (cave_oki's 'samples', part of the board's state) are never changed by them:
   only the mix reads these */
static float group_level[CAVE_SOUND_GROUPS] = { 1.0f, 1.0f, 1.0f };
static bool levels_all_one = true;
static float heard[2][CAVE_SOUND_FRAME_MOST];
static float heard_now;    /* (oki_sample's sum at the groups' levels, with its sample) */

/* each voice's group, found as its sound starts (and again after a save state's load: the sound it plays then) */
static struct
{
	uint32_t base, count;
	uint8_t group;
} voice_groups[2][4];
static float lofi_table[2][0x1001];

/* the high-pass filter (a biquad, in floats as MAME's): its coefficients and the last values in and out */
static float hp_b0, hp_b1, hp_b2, hp_a1, hp_a2;
static float hp_x[3], hp_y[3];

/* the 68000's time (cave_sound_clock) */
static uint64_t (*clock_now)(void);
static uint64_t clock_frame;

static void make_tables(void)
{
	static const int nbl2bit[16][4] = {
		{ 1, 0, 0, 0 }, { 1, 0, 0, 1 }, { 1, 0, 1, 0 }, { 1, 0, 1, 1 },
		{ 1, 1, 0, 0 }, { 1, 1, 0, 1 }, { 1, 1, 1, 0 }, { 1, 1, 1, 1 },
		{ -1, 0, 0, 0 }, { -1, 0, 0, 1 }, { -1, 0, 1, 0 }, { -1, 0, 1, 1 },
		{ -1, 1, 0, 0 }, { -1, 1, 0, 1 }, { -1, 1, 1, 0 }, { -1, 1, 1, 1 }
	};

	for (int step = 0; step <= 48; step++)
	{
		const int stepval = (int)floor(16.0 * pow(11.0 / 10.0, (double)step));

		for (int nib = 0; nib < 16; nib++)
			diff_lookup[step * 16 + nib] = nbl2bit[nib][0] *
				(stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2] + stepval / 4 * nbl2bit[nib][3] + stepval / 8);
	}

	/* audio_resampler_lofi's interpolation table: f0(t) = (t - t^3) / 6, f1(t) = t + (t^2 - t^3) / 2, f1 past the
	   middle made so that the four add up to 1 */
	for (int i = 1; i != 4096; i++)
	{
		const float p = (float)i / 4096.0f;

		lofi_table[0][i] = (p - p * p * p) / 6;
	}
	for (int i = 1; i != 2049; i++)
	{
		const float p = (float)i / 4096.0f;

		lofi_table[1][i] = p + (p * p - p * p * p) / 2;
	}
	for (int i = 2049; i != 4096; i++)
		lofi_table[1][i] = 1.0f + lofi_table[0][i] + lofi_table[0][4096 - i] - lofi_table[1][4096 - i];
	lofi_table[0][0] = 0.0f;
	lofi_table[0][0x1000] = 0.0f;
	lofi_table[1][0] = 0.0f;
	lofi_table[1][0x1000] = 1.0f;

	/* audio_effect_filter::build_highpass */
	{
		const float rate = (float)CAVE_SOUND_RATE;
		const float k = tanf(3.14159265358979f * (float)HIGH_PASS_HZ / rate), k2 = k * k, q = HIGH_PASS_Q;
		const float d = k2 * q + k + q;

		hp_b0 = q / d;
		hp_b1 = -2 * q / d;
		hp_b2 = hp_b0;
		hp_a1 = 2 * q * (k2 - 1) / d;
		hp_a2 = (k2 * q - k + q) / d;
	}
	tables_made = true;
}

void cave_sound_clock(uint64_t (*cycles_now)(void), uint64_t frame_cycles)
{
	clock_now = cycles_now;
	clock_frame = frame_cycles;
	cave_clock = cycles_now;
}

/* the frame's samples counted (the chip's clock over 132, kept exact from frame to frame), none made yet */
static void begin_frame(void)
{
	const uint64_t per_sample = (uint64_t)OKI_DIVISOR * 1000000u;

	for (int chip = 0; chip < 2; chip++)
	{
		cave_oki *const oki = &cave_board.sound.oki[chip];

		oki->clocks += (uint64_t)SOUND->clock[chip] * FRAME_USEC;
		oki->due = (uint32_t)(oki->clocks / per_sample);
		oki->clocks %= per_sample;
		if (oki->due > CAVE_SOUND_FRAME_MOST)
			oki->due = CAVE_SOUND_FRAME_MOST;
		oki->made = 0;
	}
	cave_board.sound.frame_began = clock_now ? clock_now() : 0u;
	cave_board.sound.frame_begun = true;
}

static int oki_sample(int chip);

/* the chips' samples made up to now (their share of the frame's, by the 68000's time), or to the frame's end */
static void catch_up(bool to_the_end)
{
	uint64_t elapsed = 0;

	if (!rom_set.oki[0] || !rom_set.oki[1])
		return;
	if (!tables_made)
		make_tables();
	if (!cave_board.sound.frame_begun)
		begin_frame();
	if (!to_the_end)
	{
		uint64_t now;

		if (!clock_now || !clock_frame)
			return;    /* (no time inside the frame: all of it at the end) */
		now = clock_now();
		elapsed = now > cave_board.sound.frame_began ? now - cave_board.sound.frame_began : 0u;
	}
	for (int chip = 0; chip < 2; chip++)
	{
		cave_oki *const oki = &cave_board.sound.oki[chip];
		const uint32_t want = to_the_end || elapsed >= clock_frame ? oki->due
			: (uint32_t)((uint64_t)oki->due * elapsed / clock_frame);

		while (oki->made < want)
		{
			oki->samples[oki->made] = oki_sample(chip);
			heard[chip][oki->made++] = heard_now;
		}
	}
}

/* ---- the NMK112 ---- */

/* the page of the ROM a bank register's value selects: the ROM's pages, rounded up to a power of 2, are the
   entries; an entry past the ROM folds back onto it (its highest bit dropped) */
static uint32_t page_of(int chip, uint8_t value)
{
	uint32_t pages, mask, page;

	pages = chip_pages[chip];
	mask = chip_mask[chip];
	page = value & mask;
	while (page >= pages)
	{
		uint32_t high = mask + 1u;

		while (!(page & high))
			high >>= 1;
		page &= ~high;
	}
	return page;
}

/* a byte of the chip's 256 KB: four banks of 64 KB; on #1 (paged) the sample table's four quarters (0x000-0x3FF)
   come with the banks too, each from its own bank's page */
static uint8_t oki_byte(int chip, uint32_t address)
{
	const uint8_t *region;
	const unsigned bank = (address >> 16) & 3u;

	if (pages_for != cave_board_description)
		count_pages();
	region = rom_set.oki[chip] + chip_skip;
	address &= 0x3FFFFu;
	if ((chip_paged >> chip & 1u) && address < 0x400u)
		return region[(page_of(chip, cave_board.sound.bank[4 + (address >> 8)]) << 16) | address];
	return region[(page_of(chip, cave_board.sound.bank[chip * 4 + bank]) << 16) | (address & 0xFFFFu)];
}

/* ---- the sounds' groups ---- */

void cave_sound_set_level(cave_sound_group group, float level)
{
	if (group < 0 || group >= CAVE_SOUND_GROUPS)
		return;
	group_level[group] = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
	levels_all_one = group_level[0] == 1.0f && group_level[1] == 1.0f && group_level[2] == 1.0f;
}

/* the group of an OKI voice's sound: its start in the chip's ROM (the NMK112's page of the bank it is in), looked up
   in the description's list, or its chip's */
static uint8_t oki_voice_group(int chip, int number, const cave_oki_voice *voice)
{
	if (voice_groups[chip][number].base != voice->base || voice_groups[chip][number].count != voice->count)
	{
		const uint32_t at = page_of(chip, cave_board.sound.bank[chip * 4 + ((voice->base >> 16) & 3u)]) << 16
			| (voice->base & 0xFFFFu);
		uint8_t group = SOUND->chip_group[chip];

		for (int index = 0; index < SOUND->sample_count; index++)
			if (SOUND->samples[index].chip == chip && SOUND->samples[index].at == at)
			{
				group = SOUND->samples[index].group;
				break;
			}
		voice_groups[chip][number].base = voice->base;
		voice_groups[chip][number].count = voice->count;
		voice_groups[chip][number].group = group < CAVE_SOUND_GROUPS ? group : CAVE_SOUND_EFFECTS;
	}
	return voice_groups[chip][number].group;
}

/* ---- the YMZ280B ---- */

#define YMZ             (&cave_board.sound.ymz280b)
#define YMZ_FRAC_BITS   9
#define YMZ_FRAC_ONE    (1 << YMZ_FRAC_BITS)
#define YMZ_RING        8192u               /* the chip's last samples kept for the output's resampler (a power of 2) */
#define NONE            UINT64_MAX

/* the chip's samples in 68000 cycles: 16 MHz / 88,200 Hz = 80000 / 441 */
#define YMZ_CYCLES      80000u
#define YMZ_SAMPLES     441u

static const int ymz_index_scale[8] = { 0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266 };
static float ymz_ring[YMZ_RING];    /* (its samples, mono: both outputs, MAME's levels; by their number) */

void (*cave_sound_events_changed)(void);


static bool ymz_board(void)
{
	return SOUND->kind == CAVE_SOUND_YMZ280B && rom_set.oki[0] != NULL;
}

static uint8_t ymz_byte(uint32_t address)
{
	const uint32_t size = rom_set_region_size(CAVE_ROM_SOUND0);

	address &= 0xFFFFFFu;
	if (size && !(size & (size - 1u)))
		address &= size - 1u;    /* (a ROM of a power of 2 mirrored over the chip's 16 MB, as MAME's rom interface) */
	return address < size ? rom_set.oki[0][address] : 0u;
}

static void ymz_update_irq(void)
{
	cave_ymz *const chip = YMZ;
	const int bits = chip->irq_enable ? chip->status & chip->irq_mask : 0;

	chip->irq_state = bits ? 1u : 0u;
}

/* update_step and update_volumes */
static void ymz_update_step(cave_ymz_voice *voice)
{
	voice->output_step = (voice->mode == 1 ? voice->fnum & 0x0FF : voice->fnum & 0x1FF) + 1;
}

static void ymz_update_volumes(cave_ymz_voice *voice)
{
	if (voice->pan == 8)
	{
		voice->output_left = voice->level;
		voice->output_right = voice->level;
	}
	else if (voice->pan < 8)
	{
		voice->output_left = voice->level;
		voice->output_right = voice->pan == 0 ? 0 : voice->level * (voice->pan - 1) / 7;
	}
	else
	{
		voice->output_left = voice->level * (15 - voice->pan) / 7;
		voice->output_right = voice->level;
	}
}

/* The first sample at which a voice whose end interrupts can end: a voice reads at most one of its own samples in
   each of the chip's (its step is at most FRAC_ONE), and comes to its stop (or, looping, back from its loop's end)
   after its nibbles there at the soonest. A bound, looked at again when the chip gets there */
static void ymz_find_check(void)
{
	cave_ymz *const chip = YMZ;
	uint64_t first = NONE;

	if (chip->irq_enable)
		for (int number = 0; number < 8; number++)
		{
			const cave_ymz_voice *const voice = &chip->voice[number];
			const uint32_t unit = voice->mode == 1 ? 1u : voice->mode == 2 ? 2u : 4u;
			uint32_t distance, reads;

			if (!voice->playing || !(chip->irq_mask >> number & 1u) || voice->mode == 0)
				continue;
			distance = voice->stop > voice->position ? voice->stop - voice->position : 0u;
			if (voice->looping && voice->keyon)
			{
				/* (back from the loop's end to its start: its start may be past the stop) */
				const uint32_t to_loop_end = voice->loop_end > voice->position ? voice->loop_end - voice->position : 0u;

				if (to_loop_end < distance)
					distance = to_loop_end;
			}
			reads = (distance + unit - 1u) / unit;
			if (reads < 1u)
				reads = 1u;
			if (chip->samples + reads - 1u < first)
				first = chip->samples + reads - 1u;
		}
	chip->check = first;
}

/* a voice's next sample of its own (generate_adpcm, generate_pcm8, generate_pcm16, one sample at a time); after its
   end, MAME's ramp back to 0 */
static int ymz_generate(int number)
{
	cave_ymz *const chip = YMZ;
	cave_ymz_voice *const voice = &chip->voice[number];
	int value;

	if (!voice->playing)
	{
		int t = voice->curr_sample;

		if (t < 0)
			t = -((-t * 15) >> 4);
		else if (t > 0)
			t = (t * 15) >> 4;
		return t;
	}
	switch (voice->mode)
	{
	case 1:
	{
		const int nibble = ymz_byte(voice->position / 2u) >> ((~voice->position & 1u) << 2);
		int signal = voice->signal, step = voice->step;
		const int diff = ((nibble & 7) * 2 + 1) * ((nibble & 8) ? -1 : 1);

		signal += (step * diff) / 8;
		if (signal > 32767)
			signal = 32767;
		else if (signal < -32768)
			signal = -32768;
		step = (step * ymz_index_scale[nibble & 7]) >> 8;
		if (step > 0x6000)
			step = 0x6000;
		else if (step < 0x7F)
			step = 0x7F;
		value = signal;
		voice->position++;
		if (voice->looping)
		{
			if (voice->position == voice->loop_start && voice->loop_count == 0)
			{
				voice->loop_signal = signal;
				voice->loop_step = step;
			}
			if (voice->position >= voice->loop_end && voice->keyon)
			{
				voice->position = voice->loop_start;
				signal = voice->loop_signal;
				step = voice->loop_step;
				voice->loop_count++;
			}
		}
		voice->signal = signal;
		voice->step = step;
		break;
	}
	case 2:
		value = (int8_t)ymz_byte(voice->position / 2u) * 256;
		voice->position += 2u;
		if (voice->looping && voice->position >= voice->loop_end && voice->keyon)
			voice->position = voice->loop_start;
		break;
	case 3:
		value = (int16_t)((ymz_byte(voice->position / 2u + 1u) << 8) + ymz_byte(voice->position / 2u));
		voice->position += 4u;
		if (voice->looping && voice->position >= voice->loop_end && voice->keyon)
			voice->position = voice->loop_start;
		break;
	default:
		return 0;
	}
	if (voice->position >= voice->stop)
	{
		/* the end: the voice stops, its bit in the status, and the interrupt if the program lets it through */
		voice->playing = 0;
		chip->status |= (uint8_t)(1u << number);
		ymz_update_irq();
	}
	return value;
}

/* each voice's group (cave_sound_set_level), from where its sound starts: looked up again when that changes (a new
   sound, or a save state's load). The ring takes the voices at their groups' levels; the chip's own state never
   does, so the board plays the same whatever the levels */
static struct
{
	uint32_t start;
	uint8_t group;
} ymz_groups[8] = {
	{ UINT32_MAX, 0 }, { UINT32_MAX, 0 }, { UINT32_MAX, 0 }, { UINT32_MAX, 0 },
	{ UINT32_MAX, 0 }, { UINT32_MAX, 0 }, { UINT32_MAX, 0 }, { UINT32_MAX, 0 },
};

static uint8_t ymz_voice_group(int number, const cave_ymz_voice *voice)
{
	if (ymz_groups[number].start != voice->start)
	{
		const uint32_t at = voice->start / 2u;    /* (its byte in CAVE_ROM_SOUND0: 'start' counts nibbles) */
		uint8_t group = SOUND->chip_group[0];

		for (int index = 0; index < SOUND->sample_count; index++)
			if (SOUND->samples[index].chip == 0 && SOUND->samples[index].at == at)
			{
				group = SOUND->samples[index].group;
				break;
			}
		ymz_groups[number].start = voice->start;
		ymz_groups[number].group = group;
	}
	return ymz_groups[number].group;
}

/* the chip's next sample (sound_stream_update, a sample at a time): each voice's own samples to the chip's rate by
   MAME's linear interpolation, at its levels, both outputs (MAME's speaker is mono: the two added) */
static void ymz_sample(void)
{
	cave_ymz *const chip = YMZ;
	float mixed = 0.0f;
	bool ended = false;

	for (int number = 0; number < 8; number++)
	{
		cave_ymz_voice *const voice = &chip->voice[number];
		int interpolated;

		if (!voice->playing && voice->curr_sample == 0 && voice->last_sample == 0)
		{
			voice->output_pos = YMZ_FRAC_ONE;    /* (so that the next sound plays at once) */
			continue;
		}
		if (voice->output_pos >= YMZ_FRAC_ONE)
		{
			const bool was_playing = voice->playing;

			voice->output_pos -= YMZ_FRAC_ONE;
			voice->last_sample = voice->curr_sample;
			voice->curr_sample = (int16_t)ymz_generate(number);
			ended |= was_playing && !voice->playing;
		}
		interpolated = (voice->last_sample * (YMZ_FRAC_ONE - voice->output_pos) + voice->curr_sample * voice->output_pos)
			>> YMZ_FRAC_BITS;
		{
			const float both = (float)(interpolated * voice->output_left / 2) / (32768.0f * 256.0f)
				+ (float)(interpolated * voice->output_right / 2) / (32768.0f * 256.0f);

			mixed += levels_all_one ? both : both * group_level[ymz_voice_group(number, voice)];
		}
		voice->output_pos += voice->output_step;
	}
	ymz_ring[chip->samples & (YMZ_RING - 1u)] = mixed;
	chip->samples++;
	if (ended || chip->samples > chip->check)
		ymz_find_check();
}

void cave_sound_run_to(uint64_t cycles)
{
	cave_ymz *const chip = YMZ;
	const uint64_t through = cycles / YMZ_CYCLES * YMZ_SAMPLES + cycles % YMZ_CYCLES * YMZ_SAMPLES / YMZ_CYCLES;

	if (!ymz_board())
		return;
	while (chip->samples <= through)
		ymz_sample();
}

bool cave_sound_irq_at(uint64_t cycles)
{
	cave_ymz *const chip = YMZ;
	cave_ymz kept;
	bool irq;

	if (!ymz_board())
		return false;
	kept = *chip;
	cave_sound_run_to(cycles);
	irq = chip->irq_state != 0;
	*chip = kept;    /* (its samples made on the way are in the ring: made again, the same or as a write since changes them) */
	return irq;
}

uint64_t cave_sound_event_at(void)
{
	const uint64_t check = YMZ->check;

	if (!ymz_board() || check == NONE)
		return NONE;
	/* (the first cycle the sample's time has come by: sample n at n * 80000 / 441) */
	return check / YMZ_SAMPLES * YMZ_CYCLES + (check % YMZ_SAMPLES * YMZ_CYCLES + YMZ_SAMPLES - 1u) / YMZ_SAMPLES;
}

/* the 68000's time for an access: the instruction's start */
static void ymz_catch_up(void)
{
	if (cave_access_clock)
		cave_sound_run_to(cave_access_clock());
	else if (cave_clock)
		cave_sound_run_to(cave_clock());
}

static void ymz_changed(void)
{
	if (cave_sound_events_changed)
		cave_sound_events_changed();
}

/* write_to_register */
static void ymz_write_register(int data)
{
	cave_ymz *const chip = YMZ;
	const int reg = chip->current_register;

	if (reg < 0x80)
	{
		cave_ymz_voice *const voice = &chip->voice[(reg >> 2) & 7];

		switch (reg & 0xE3)
		{
		case 0x00:
			voice->fnum = (uint16_t)((voice->fnum & 0x100) | (data & 0xFF));
			ymz_update_step(voice);
			break;
		case 0x01:
			voice->fnum = (uint16_t)((voice->fnum & 0xFF) | ((data & 0x01) << 8));
			voice->looping = (uint8_t)((data & 0x10) >> 4);
			if ((data & 0x60) == 0)
				data &= 0x7F;    /* (no mode: as a key off) */
			else
				voice->mode = (uint8_t)((data & 0x60) >> 5);
			if (!voice->keyon && (data & 0x80) && chip->keyon_enable)
			{
				voice->playing = 1;
				voice->position = voice->start;
				voice->signal = voice->loop_signal = 0;
				voice->step = voice->loop_step = 0x7F;
				voice->loop_count = 0;
			}
			else if (voice->keyon && !(data & 0x80))
				voice->playing = 0;
			voice->keyon = (uint8_t)((data & 0x80) >> 7);
			ymz_update_step(voice);
			break;
		case 0x02:
			voice->level = (uint8_t)data;
			ymz_update_volumes(voice);
			break;
		case 0x03:
			voice->pan = (uint8_t)(data & 0x0F);
			ymz_update_volumes(voice);
			break;
		case 0x20: voice->start = (voice->start & (0x00FFFFu << 1)) | ((uint32_t)data << 17); break;
		case 0x21: voice->loop_start = (voice->loop_start & (0x00FFFFu << 1)) | ((uint32_t)data << 17); break;
		case 0x22: voice->loop_end = (voice->loop_end & (0x00FFFFu << 1)) | ((uint32_t)data << 17); break;
		case 0x23: voice->stop = (voice->stop & (0x00FFFFu << 1)) | ((uint32_t)data << 17); break;
		case 0x40: voice->start = (voice->start & (0xFF00FFu << 1)) | ((uint32_t)data << 9); break;
		case 0x41: voice->loop_start = (voice->loop_start & (0xFF00FFu << 1)) | ((uint32_t)data << 9); break;
		case 0x42: voice->loop_end = (voice->loop_end & (0xFF00FFu << 1)) | ((uint32_t)data << 9); break;
		case 0x43: voice->stop = (voice->stop & (0xFF00FFu << 1)) | ((uint32_t)data << 9); break;
		case 0x60: voice->start = (voice->start & (0xFFFF00u << 1)) | ((uint32_t)data << 1); break;
		case 0x61: voice->loop_start = (voice->loop_start & (0xFFFF00u << 1)) | ((uint32_t)data << 1); break;
		case 0x62: voice->loop_end = (voice->loop_end & (0xFFFF00u << 1)) | ((uint32_t)data << 1); break;
		case 0x63: voice->stop = (voice->stop & (0xFFFF00u << 1)) | ((uint32_t)data << 1); break;
		default: break;
		}
		return;
	}
	switch (reg)
	{
	case 0x84:
		chip->ext_mem_address_hi = (uint32_t)data << 16;
		break;
	case 0x85:
		chip->ext_mem_address_mid = (uint32_t)data << 8;
		break;
	case 0x86:
		chip->ext_mem_address = chip->ext_mem_address_hi | chip->ext_mem_address_mid | (uint32_t)data;
		if (chip->ext_mem_enable)
			chip->ext_readlatch = ymz_byte(chip->ext_mem_address);
		break;
	case 0x87:
		if (chip->ext_mem_enable)
			chip->ext_mem_address = (chip->ext_mem_address + 1u) & 0xFFFFFFu;    /* (a RAM write: the board has ROM) */
		break;
	case 0xFE:
		chip->irq_mask = (uint8_t)data;
		ymz_update_irq();
		break;
	case 0xFF:
		chip->ext_mem_enable = (uint8_t)((data & 0x40) >> 6);
		chip->irq_enable = (uint8_t)((data & 0x10) >> 4);
		ymz_update_irq();
		if (chip->keyon_enable && !(data & 0x80))
			for (int number = 0; number < 8; number++)
				chip->voice[number].playing = 0;
		else if (!chip->keyon_enable && (data & 0x80))
			for (int number = 0; number < 8; number++)
				if (chip->voice[number].keyon && chip->voice[number].looping)
					chip->voice[number].playing = 1;
		chip->keyon_enable = (uint8_t)((data & 0x80) >> 7);
		break;
	default:
		break;    /* (the DSP's 80-82: not on MAME's chip either) */
	}
}

uint8_t cave_sound_ymz_read(int port)
{
	cave_ymz *const chip = YMZ;
	uint8_t result;

	if (!ymz_board())
		return port ? 0u : 0xFFu;
	ymz_catch_up();
	if (port == 0)
	{
		if (!chip->ext_mem_enable)
			return 0xFFu;
		result = chip->ext_readlatch;
		chip->ext_readlatch = ymz_byte(chip->ext_mem_address);
		chip->ext_mem_address = (chip->ext_mem_address + 1u) & 0xFFFFFFu;
		return result;
	}
	/* compute_status: the status, cleared with the interrupt */
	result = chip->status;
	chip->status = 0;
	ymz_update_irq();
	ymz_changed();
	return result;
}

void cave_sound_ymz_write(int port, uint8_t data)
{
	if (port == 0)
		cave_board.sound.ymz_register = data;
	else
		cave_board.sound.ymz[cave_board.sound.ymz_register] = data;
	if (!ymz_board())
		return;
	if (port == 0)
	{
		YMZ->current_register = data;
		return;
	}
	ymz_catch_up();
	ymz_write_register(data);
	ymz_find_check();
	ymz_changed();
}

/* device_reset: every register written 0 from FF down, the voices silent */
static void ymz_reset(void)
{
	cave_ymz *const chip = YMZ;

	memset(chip, 0, sizeof(*chip));
	for (int reg = 0xFF; reg >= 0; reg--)
	{
		chip->current_register = (uint8_t)reg;
		ymz_write_register(0);
	}
	chip->current_register = 0;
	chip->status = 0;
	chip->ext_mem_address = 0;
	for (int number = 0; number < 8; number++)
	{
		chip->voice[number].curr_sample = 0;
		chip->voice[number].last_sample = 0;
		chip->voice[number].output_pos = YMZ_FRAC_ONE;
		chip->voice[number].playing = 0;
	}
	chip->check = NONE;
	memset(ymz_ring, 0, sizeof(ymz_ring));
}

void cave_sound_reset(void)
{
	memset(&cave_board.sound, 0, sizeof(cave_board.sound));
	cave_board.sound.oki[0].command = -1;
	cave_board.sound.oki[1].command = -1;
	ymz_reset();
}

void cave_sound_bank(int which, uint8_t data)
{
	catch_up(false);
	cave_board.sound.bank[which & 7] = data;
}

/* ---- the M6295s ---- */

uint8_t cave_sound_status(int chip)
{
	uint8_t result = 0xF0u;

	catch_up(false);    /* (as okim6295_device::read: the stream brought up to now) */
	for (int voice = 0; voice < 4; voice++)
		if (cave_board.sound.oki[chip].voice[voice].playing)
			result |= (uint8_t)(1u << voice);
	return result;
}

void cave_sound_command(int chip, uint8_t data)
{
	cave_oki *const oki = &cave_board.sound.oki[chip];

	catch_up(false);
	if (oki->command != -1)
	{
		/* a play command's second byte: the voices (bits 4-7) and the volume */
		unsigned mask = data >> 4;

		for (int number = 0; number < 4; number++, mask >>= 1)
			if (mask & 1u)
			{
				cave_oki_voice *const voice = &oki->voice[number];
				const uint32_t base = (uint32_t)oki->command * 8u;
				uint32_t start, stop;

				if (voice->playing)
					continue;
				start = (uint32_t)oki_byte(chip, base) << 16 | (uint32_t)oki_byte(chip, base + 1u) << 8
					| oki_byte(chip, base + 2u);
				stop = (uint32_t)oki_byte(chip, base + 3u) << 16 | (uint32_t)oki_byte(chip, base + 4u) << 8
					| oki_byte(chip, base + 5u);
				start &= 0x3FFFFu;
				stop &= 0x3FFFFu;
				if (start < stop)
				{
					voice->playing = true;
					voice->base = start;
					voice->sample = 0;
					voice->count = 2u * (stop - start + 1u);
					voice->signal = 0;
					voice->step = 0;
					voice->volume = volume_table[data & 0x0Fu];
					oki_voice_group(chip, number, voice);    /* (its group, from the banks as it starts) */
				}
			}
		oki->command = -1;
	}
	else if (data & 0x80u)
		oki->command = data & 0x7F;
	else
	{
		/* stop the voices of bits 3-6 */
		unsigned mask = data >> 3;

		for (int number = 0; number < 4; number++, mask >>= 1)
			if (mask & 1u)
				oki->voice[number].playing = false;
	}
}

/* the chip's next sample, its voices' sum: 2048 * 32 to a voice at full volume */
static int oki_sample(int chip)
{
	cave_oki *const oki = &cave_board.sound.oki[chip];
	int sum = 0;
	float at_levels = 0.0f;

	for (int number = 0; number < 4; number++)
	{
		cave_oki_voice *const voice = &oki->voice[number];
		int nibble;

		if (!voice->playing)
			continue;
		nibble = (oki_byte(chip, voice->base + voice->sample / 2u) >> (((voice->sample & 1u) << 2) ^ 4u)) & 15;
		voice->signal += diff_lookup[voice->step * 16 + nibble];
		if (voice->signal > 2047)
			voice->signal = 2047;
		else if (voice->signal < -2048)
			voice->signal = -2048;
		voice->step += index_shift[nibble & 7];
		if (voice->step > 48)
			voice->step = 48;
		else if (voice->step < 0)
			voice->step = 0;
		sum += voice->signal * voice->volume;
		if (!levels_all_one)
			at_levels += (float)(voice->signal * voice->volume) * group_level[oki_voice_group(chip, number, voice)];
		if (++voice->sample >= voice->count)
			voice->playing = false;
	}
	heard_now = levels_all_one ? (float)sum : at_levels;
	return sum;
}

/* ---- the frame ---- */

static void output_put(int16_t sample)
{
	if (output_write - output_read >= OUTPUT_SIZE)
		output_read++;    /* (full: the oldest goes) */
	output[output_write++ & (OUTPUT_SIZE - 1u)] = sample;
}

/* MAME's LoFi resampler: the chip's sample at 'position' (in its samples of this frame, from 0), 4 samples behind
   it (audio_resampler_lofi: "ssample -= 4"), between the 2nd and 3rd of the 4 before it */
static float lofi(int chip, double position)
{
	const cave_oki *const oki = &cave_board.sound.oki[chip];
	const int below = (int)floor(position);
	const unsigned phase = (unsigned)((position - below) * 4096.0) & 0xFFFu;
	float s[4];

	for (int k = 0; k < 4; k++)
	{
		const int index = below - 4 + k;

		s[k] = index < 0 ? history[chip][HISTORY + index]
			: heard[chip][index < (int)oki->made ? index : (int)oki->made - 1] / (2048.0f * 32.0f);
	}
	return -s[0] * lofi_table[0][0x1000 - phase] + s[1] * lofi_table[1][0x1000 - phase] + s[2] * lofi_table[1][phase]
		- s[3] * lofi_table[0][phase];
}

/* MAME's LoFi resampler from the YMZ280B's 88,200 Hz: more than twice the output's rate, so each pair of the chip's
   samples (from an even one) is taken as one, their average, and the output's sample comes from the 4 pairs 4 pairs
   behind it (audio_resampler_lofi with its m_source_divide of 2), each output sample's place worked out from its
   number since the reset as MAME works out a stream update's first */
#define YMZ_RATE    88200u

static float ymz_pair(uint64_t first)
{
	return (ymz_ring[first & (YMZ_RING - 1u)] + ymz_ring[(first + 1u) & (YMZ_RING - 1u)]) / 2.0f;
}

static float ymz_lofi(uint64_t output)
{
	const uint64_t seconds = output / CAVE_SOUND_RATE, within = output % CAVE_SOUND_RATE;
	const uint64_t place = within * YMZ_RATE * 0x1000u / CAVE_SOUND_RATE;
	uint64_t sample = (place >> 12) + (uint64_t)YMZ_RATE * seconds;
	uint32_t phase = (uint32_t)(place & 0xFFFu);
	const uint32_t delta = (uint32_t)(sample % 2u);

	phase = (phase | delta << 12) / 2u;
	sample -= delta;
	if (sample < 8u)
		return 0.0f;    /* (the first, from before the reset: silence) */
	sample -= 8u;
	return -ymz_pair(sample) * lofi_table[0][0x1000 - phase] + ymz_pair(sample + 2u) * lofi_table[1][0x1000 - phase]
		+ ymz_pair(sample + 4u) * lofi_table[1][phase] - ymz_pair(sample + 6u) * lofi_table[0][phase];
}

/* the speaker's high-pass, then to 16 bits as sound.cpp: the int cut toward 0, then clamped */
static void output_mixed(float mixed)
{
	int value;

	hp_x[2] = hp_x[1];
	hp_x[1] = hp_x[0];
	hp_x[0] = mixed;
	mixed = hp_b0 * hp_x[0] + hp_b1 * hp_x[1] + hp_b2 * hp_x[2] - hp_a1 * hp_y[0] - hp_a2 * hp_y[1];
	hp_y[2] = hp_y[1];
	hp_y[1] = hp_y[0];
	hp_y[0] = mixed;
	value = (int)(mixed * 32768.0f);
	output_put((int16_t)(value < -32768 ? -32768 : value > 32767 ? 32767 : value));
}

/* the YMZ280B's frame: the chip up to the VBLANK, and the output's samples whose time has come (output sample j at
   68000 cycle j * 1000 / 3), from those of the chip made by then */
static void ymz_frame(void)
{
	cave_ymz *const chip = YMZ;
	const uint64_t now = cave_clock ? cave_clock() : cave_board.frame * ((uint64_t)CAVE_CPU_CLOCK * FRAME_USEC / 1000000u);
	const uint64_t through = now / 1000u * 3u + now % 1000u * 3u / 1000u;

	if (!tables_made)
		make_tables();
	/* (the chip up to the instruction's start, as an access would: the output is 8 of its samples behind) */
	cave_sound_run_to(cave_access_clock ? cave_access_clock() : now);
	if (through + 1u < chip->outputs)
		chip->outputs = through + 1u;    /* (never: the time went back) */
	if (through + 1u - chip->outputs > YMZ_RING / 2u)
		chip->outputs = through + 1u - YMZ_RING / 2u;    /* (a jump in time: only what the ring still has) */
	while (chip->outputs <= through)
		output_mixed((float)SOUND->gain[0] * ymz_lofi(chip->outputs++));
}

bool cave_sound_quiet;

void cave_sound_frame(void)
{
	unsigned outputs;

	if (cave_sound_quiet)
		return;    /* (a frame run to be thrown away: the output's own state left as it is) */
	if (ymz_board())
	{
		ymz_frame();
		return;
	}
	if (!rom_set.oki[0] || !rom_set.oki[1])
		return;
	catch_up(true);

	output_clock += (uint64_t)CAVE_SOUND_RATE * FRAME_USEC;
	outputs = (unsigned)(output_clock / 1000000u);
	output_clock %= 1000000u;
	for (unsigned index = 0; index < outputs; index++)
	{
		float mixed = 0.0f;
		int value;

		for (int chip = 0; chip < 2; chip++)
		{
			if (cave_board.sound.oki[chip].made)
				mixed += (float)SOUND->gain[chip] * lofi(chip, resample_position[chip]);
			resample_position[chip] += (double)SOUND->clock[chip] / OKI_DIVISOR / CAVE_SOUND_RATE;
		}
		/* the speaker's high-pass (filter.h's history and apply) */
		hp_x[2] = hp_x[1];
		hp_x[1] = hp_x[0];
		hp_x[0] = mixed;
		mixed = hp_b0 * hp_x[0] + hp_b1 * hp_x[1] + hp_b2 * hp_x[2] - hp_a1 * hp_y[0] - hp_a2 * hp_y[1];
		hp_y[2] = hp_y[1];
		hp_y[1] = hp_y[0];
		hp_y[0] = mixed;
		/* (to 16 bits as sound.cpp: the int cut toward 0, then clamped) */
		value = (int)(mixed * 32768.0f);
		output_put((int16_t)(value < -32768 ? -32768 : value > 32767 ? 32767 : value));
	}
	for (int chip = 0; chip < 2; chip++)
	{
		cave_oki *const oki = &cave_board.sound.oki[chip];
		float last[HISTORY];

		/* the last 4 kept for the next frame's first outputs */
		for (int k = 0; k < HISTORY; k++)
		{
			const int index = (int)oki->made - HISTORY + k;

			last[k] = index < 0 ? history[chip][HISTORY + index] : heard[chip][index] / (2048.0f * 32.0f);
		}
		memcpy(history[chip], last, sizeof(last));
		resample_position[chip] -= oki->made;
		if (resample_position[chip] < 0.0 || resample_position[chip] > 2.0)
			resample_position[chip] = 0.0;    /* (never, but kept from wandering) */
	}
	begin_frame();
}

unsigned cave_sound_take(int16_t *out, unsigned most)
{
	unsigned count = 0;

	while (count < most && output_read != output_write)
		out[count++] = output[output_read++ & (OUTPUT_SIZE - 1u)];
	return count;
}

void cave_sound_drop(void)
{
	output_read = output_write;
}
