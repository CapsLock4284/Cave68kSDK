/**
 *    Name:        video.c
 *    Description: the picture from the board's memories (cave/video.h), as the game's description has it
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME (BSD-3-Clause, Luca Elia and others): cave_v.cpp's screen_update and tilemap_draw, sprite013.cpp's
 *    sprites (DonPachi's and DoDonPachi's: the "alt" format, no zoom), tmap038.cpp's tiles, and the layers' row-scroll
 *    and row-select (from each layer's line RAM; the 8x8-only layer has none). Not yet: flipped layers and sprites
 *    (the games set neither: their screen-flip setting is the cabinet's).
 */
#include "cave/video.h"

#include "cave/board.h"
#include "cave/machine.h"
#include "cave/rom_set.h"

#include <string.h>

/* the game's picture (cave/board.h's cave_video_desc: DonPachi's offsets 0x13 and -0x12, its layers' colours from
   400, 64 palettes of 16, its sprite RAM in banks of 0x4000 bytes) */
#define VIDEO              (cave_board_description->video)
#define MAX_SPRITE_CLOCK   (448u * 272u * 2u)    /* sprite013: what the chip can draw in a frame */

/* the colours a colour code steps by (cave_video_desc's colour_step) */
static unsigned colour_step(void)
{
	return VIDEO->colour_step ? VIDEO->colour_step : 16u;
}

/* the graphics' sizes, from the description (taken as each frame is drawn or its sprites latched) */
static uint32_t layer_tiles[4], sprites_mask;

static void take_sizes(void)
{
	for (int layer = 0; layer < 4; layer++)
		layer_tiles[layer] = rom_set_region_size(CAVE_ROM_LAYER0 + layer) / (VIDEO->eight_bits >> layer & 1u ? 64u : 32u);
	sprites_mask = rom_set_region_size(CAVE_ROM_SPRITES) - 1u;
}

static uint16_t sprite_layer[VIDEO_HEIGHT][VIDEO_WIDTH];    /* pen | colour and priority << 8, 0 for none */
static uint8_t priority[VIDEO_HEIGHT][VIDEO_WIDTH];

/* xGRB 555 to 0x00RRGGBB */
static uint32_t colour(unsigned pen)
{
	const uint16_t word = cave_board.palette[pen & (VIDEO->palette_words - 1u)];
	const uint32_t g = word >> 10 & 31u, r = word >> 5 & 31u, b = word & 31u;

	return (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}

/* ---- the tile layers (tmap038) ---- */

/* The pen of layer 'layer' at its 512 x 512 map's point (x, y), with its tile's colour and priority */
static unsigned layer_pixel(int layer, unsigned x, unsigned y, unsigned *palette_out, unsigned *priority_out)
{
	const uint16_t *const ram = cave_board.layer_ram[layer];
	const uint8_t *const gfx = rom_set.layer[layer];
	const uint32_t tiles = layer_tiles[layer];
	const int big = (cave_board.layer_regs[layer][1] & 0x2000u) != 0 && !(VIDEO->small_only >> layer & 1u);
	uint32_t tile, code;
	uint8_t byte;

	x &= 511u;
	y &= 511u;
	if (big)
	{
		const unsigned index = (y >> 4) * 32u + (x >> 4);

		tile = (uint32_t)ram[index * 2u] << 16 | ram[index * 2u + 1u];
		code = (tile & 0xFFFFu) * 4u;
		code ^= (x >> 3) & 1u;
		code ^= ((y >> 3) & 1u) * 2u;
	}
	else
	{
		const unsigned index = (y >> 3) * 64u + (x >> 3);

		tile = (uint32_t)ram[0x2000u + index * 2u] << 16 | ram[0x2000u + index * 2u + 1u];
		code = tile & 0x3FFFFu;
	}
	*palette_out = tile >> 24 & 0x3Fu;
	*priority_out = tile >> 30;
	code %= tiles;
	if (VIDEO->eight_bits >> layer & 1u)
	{
		/* (MAME's layout_8x8x8: planes 8-11 then 0-3, the pixels' x at bits 0, 4, 16, 20, 32, 36, 48, 52 of a row
		   of 64: a pixel's low nibble in one byte, its high nibble in the next, x even the bytes' high nibbles) */
		const uint8_t *const pair = &gfx[code * 64u + (y & 7u) * 8u + ((x & 7u) >> 1) * 2u];
		const unsigned shift = (x & 1u) ? 0u : 4u;

		return (unsigned)((pair[1] >> shift & 15u) << 4 | (pair[0] >> shift & 15u));
	}
	byte = gfx[code * 32u + (y & 7u) * 4u + ((x & 7u) >> 1)];
	return (x & 1u) ? (byte & 15u) : (byte >> 4);
}

/* tilemap_draw: the tiles of priority 'tile_priority' of a layer whose own priority is 'layer_priority' */
static void draw_layer(uint32_t *frame, int layer, unsigned tile_priority, unsigned layer_priority)
{
	const uint16_t *const regs = cave_board.layer_regs[layer];
	const int big = (regs[1] & 0x2000u) != 0 && !(VIDEO->small_only >> layer & 1u);
	const int row_effects = !(VIDEO->small_only >> layer & 1u);    /* (an 8x8-only layer has no line RAM) */
	int offs_x = VIDEO->layer_offs_x;
	int sx, sy;

	if ((regs[2] & 3u) != layer_priority || (regs[2] & 0x10u))
		return;
	offs_x -= big ? (layer + 1) : (layer + 1 + 8);
	sx = (int)(regs[0] & 0x1FFu) - (int)cave_board.video_regs[0] - offs_x;
	sy = (int)(regs[1] & 0x1FFu) - (int)cave_board.video_regs[1] - VIDEO->layer_offs_y;
	for (int y = 0; y < VIDEO_HEIGHT; y++)
	{
		/* row-scroll (a line's own x scroll, added) and row-select (the map's line a screen line shows), from the
		   layer's line RAM (tmap038's lineram, 0x1000 bytes into its RAM: two words a line), read at the line
		   sy - 1 + y (cave_v.cpp's m_row_effect_offs_n) */
		int line_x = sx, line_y = y + sy;

		if (row_effects && (regs[0] & 0x4000u || regs[1] & 0x4000u))
		{
			const uint16_t *const line = &cave_board.layer_ram[layer][0x800u + ((unsigned)(sy - 1 + y) & 0x1FFu) * 2u];

			if (regs[0] & 0x4000u)
				line_x += line[0];
			if (regs[1] & 0x4000u)
				line_y = line[1];
		}
		for (int x = 0; x < VIDEO_WIDTH; x++)
		{
			unsigned palette, tile_pri;
			const unsigned pen = layer_pixel(layer, (unsigned)(x + line_x), (unsigned)line_y, &palette, &tile_pri);

			if (pen == 0 || tile_pri != tile_priority)
				continue;
			frame[y * VIDEO_WIDTH + x] = colour(VIDEO->layer_colour_base
				+ (VIDEO->eight_bits >> layer & 1u ? palette * 256u : palette * colour_step()) + pen);
			priority[y][x] = (uint8_t)(tile_priority + 1u);
		}
	}
}

/* ---- the sprites (sprite013, DonPachi's format: 8 words a sprite, no zoom) ---- */

static unsigned sprite_pen(uint32_t pixel)
{
	const uint8_t byte = rom_set.sprites[(pixel >> 1) & sprites_mask];

	return ((pixel & 1u) != 0) != (VIDEO->sprite_low_first != 0) ? (byte & 15u) : (byte >> 4);
}

void video_latch_sprites(void)
{
	const unsigned bank = cave_board.video_regs[4] & 3u;
	const uint16_t *source = &cave_board.sprite_ram[VIDEO->sprite_bank_words * bank];
	const uint16_t *const finish = source + VIDEO->sprite_bank_words;
	uint32_t clock = 0;

	take_sizes();
	memset(sprite_layer, 0, sizeof(sprite_layer));
	for (; source < finish; source += 8)
	{
		const uint16_t attr = source[0];
		const uint32_t code = source[1] + ((uint32_t)(attr & 3u) << 16);
		int x = source[2] & 0x3FF, y = source[3] & 0x3FF;
		const uint16_t size = source[4];
		const int width = (size >> 8 & 0x1F) * 16, height = (size & 0x1F) * 16;
		const int flipx = (attr & 8u) != 0, flipy = (attr & 4u) != 0;
		const uint16_t colpri = (uint16_t)((attr & 0x0030u) << 10 | (attr & 0x3F00u));

		clock += 32u;
		if (clock > MAX_SPRITE_CLOCK)
			break;
		if (x & 0x200)
			x -= 0x400;
		if (y & 0x200)
			y -= 0x400;
		if (!width || !height || x + width <= 0 || x >= VIDEO_WIDTH || y + height <= 0 || y >= VIDEO_HEIGHT)
			continue;
		clock += (uint32_t)(width * height);
		if (clock > MAX_SPRITE_CLOCK)
			break;
		for (int row = 0; row < height; row++)
		{
			const int draw_y = y + row;
			const int src_y = flipy ? height - 1 - row : row;

			if (draw_y < 0 || draw_y >= VIDEO_HEIGHT)
				continue;
			for (int column = 0; column < width; column++)
			{
				const int draw_x = x + column;
				const int src_x = flipx ? width - 1 - column : column;
				unsigned pen;

				if (draw_x < 0 || draw_x >= VIDEO_WIDTH)
					continue;
				pen = sprite_pen((code << 8) + (uint32_t)(src_y * width + src_x));
				if (pen)
					sprite_layer[draw_y][draw_x] = (uint16_t)(pen | colpri);
			}
		}
	}
}

/* sprite013's draw with cave_v.cpp's colpri_cb: a sprite of priority p goes over the tiles of priorities below p */
static void draw_sprites(uint32_t *frame)
{
	for (int y = 0; y < VIDEO_HEIGHT; y++)
		for (int x = 0; x < VIDEO_WIDTH; x++)
		{
			const uint16_t pixel = sprite_layer[y][x];
			unsigned colpri, pri;

			if (!pixel)
				continue;
			colpri = pixel >> 8;
			pri = colpri >> 6 & 3u;
			if (priority[y][x] <= pri)
			{
				frame[y * VIDEO_WIDTH + x] = colour((pixel & 0xFFu) + (colpri & 0x3Fu) * colour_step());
				priority[y][x] = 0xFF;
			}
		}
}

void video_draw(uint32_t *frame)
{
	const uint32_t background = colour(VIDEO->background_pen);

	take_sizes();
	for (int index = 0; index < VIDEO_WIDTH * VIDEO_HEIGHT; index++)
		frame[index] = background;
	memset(priority, 0, sizeof(priority));
	for (unsigned pri = 0; pri <= 3u; pri++)
		for (unsigned pri2 = 0; pri2 <= 3u; pri2++)
			for (int layer = 0; layer < VIDEO->layers; layer++)
				draw_layer(frame, layer, pri, pri2);
	draw_sprites(frame);
}

void video_rotated(const uint32_t *frame, uint32_t *picture)
{
	/* (ROT270: the board's top row is the picture's left column, read from the bottom up) */
	for (int y = 0; y < VIDEO_HEIGHT; y++)
		for (int x = 0; x < VIDEO_WIDTH; x++)
			picture[(VIDEO_WIDTH - 1 - x) * VIDEO_HEIGHT + y] = frame[y * VIDEO_WIDTH + x];
}
