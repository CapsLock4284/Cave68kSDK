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
 *    17-11-2026: the layers in one pass (a line of each fetched a tile at a time, each pixel the layer MAME's order
 *    draws last), the sprites clipped to the screen once: the same pictures (every frame of 14,000 of DoDonPachi's and
 *    DonPachi's), 3.7 times faster (4.1 ms a frame to 1.1).
 *    20-11-2026: the planes (video_draw_planes, video_compose): each layer and the sprites apart, for the ports'
 *    pixel-art scalers to make larger one by one (cave/video.h).
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

/* A screen line of a layer: each pixel's pen (0: none), and its tile's colour and priority */
typedef struct
{
	uint8_t pen[VIDEO_WIDTH], palette[VIDEO_WIDTH], tile_priority[VIDEO_WIDTH];
} layer_line;

/* Layer 'layer''s line: its 512 x 512 map's pixels from (line_x, map_y) on, a tile (8 pixels) fetched at a time */
static void fetch_line(int layer, int line_x, unsigned map_y, layer_line *out)
{
	const uint16_t *const ram = cave_board.layer_ram[layer];
	const uint8_t *const gfx = rom_set.layer[layer];
	const uint32_t tiles = layer_tiles[layer];
	const int big = (cave_board.layer_regs[layer][1] & 0x2000u) != 0 && !(VIDEO->small_only >> layer & 1u);
	const int eight = (VIDEO->eight_bits >> layer & 1u) != 0;
	const unsigned y = map_y & 511u;
	int x = 0;

	while (x < VIDEO_WIDTH)
	{
		const unsigned mx = (unsigned)(x + line_x) & 511u;
		const int run = 8 - (int)(mx & 7u);    /* (the pixels left in this 8 x 8 tile) */
		uint32_t tile, code;
		uint8_t palette, tile_priority;

		if (big)
		{
			const unsigned index = (y >> 4) * 32u + (mx >> 4);

			tile = (uint32_t)ram[index * 2u] << 16 | ram[index * 2u + 1u];
			code = (tile & 0xFFFFu) * 4u;
			code ^= (mx >> 3) & 1u;
			code ^= ((y >> 3) & 1u) * 2u;
		}
		else
		{
			const unsigned index = (y >> 3) * 64u + (mx >> 3);

			tile = (uint32_t)ram[0x2000u + index * 2u] << 16 | ram[0x2000u + index * 2u + 1u];
			code = tile & 0x3FFFFu;
		}
		palette = (uint8_t)(tile >> 24 & 0x3Fu);
		tile_priority = (uint8_t)(tile >> 30);
		code %= tiles;
		for (int k = 0; k < run && x < VIDEO_WIDTH; k++, x++)
		{
			const unsigned px = (mx & 7u) + (unsigned)k;
			unsigned pen;

			if (eight)
			{
				/* (MAME's layout_8x8x8: planes 8-11 then 0-3, the pixels' x at bits 0, 4, 16, 20, 32, 36, 48, 52
				   of a row of 64: a pixel's low nibble in one byte, its high nibble in the next, x even the bytes'
				   high nibbles) */
				const uint8_t *const pair = &gfx[code * 64u + (y & 7u) * 8u + (px >> 1) * 2u];
				const unsigned shift = (px & 1u) ? 0u : 4u;

				pen = (unsigned)((pair[1] >> shift & 15u) << 4 | (pair[0] >> shift & 15u));
			}
			else
			{
				const uint8_t byte = gfx[code * 32u + (y & 7u) * 4u + (px >> 1)];

				pen = (px & 1u) ? (byte & 15u) : (byte >> 4);
			}
			out->pen[x] = (uint8_t)pen;
			out->palette[x] = palette;
			out->tile_priority[x] = tile_priority;
		}
	}
}

/* tilemap_draw for every layer, as MAME draws them: the tiles of priority 0 to 3, each the layers of priority 0 to 3,
   each layer by its number, each over what came before. Done in one pass: a pixel shows the last of those orders
   that has a pen there, the one with the greatest (tile priority, layer priority, layer) */
static void draw_layers(uint32_t *frame, uint32_t *const *planes)
{
	static layer_line lines[4];
	int on[4], layer_priority[4], sx[4], sy[4], row_effects[4];
	const int layers = VIDEO->layers;

	for (int layer = 0; layer < layers; layer++)
	{
		const uint16_t *const regs = cave_board.layer_regs[layer];
		const int big = (regs[1] & 0x2000u) != 0 && !(VIDEO->small_only >> layer & 1u);
		const int offs_x = VIDEO->layer_offs_x - (big ? (layer + 1) : (layer + 1 + 8));

		on[layer] = !(regs[2] & 0x10u);
		layer_priority[layer] = regs[2] & 3u;
		row_effects[layer] = !(VIDEO->small_only >> layer & 1u);    /* (an 8x8-only layer has no line RAM) */
		sx[layer] = (int)(regs[0] & 0x1FFu) - (int)cave_board.video_regs[0] - offs_x;
		sy[layer] = (int)(regs[1] & 0x1FFu) - (int)cave_board.video_regs[1] - VIDEO->layer_offs_y;
	}
	for (int y = 0; y < VIDEO_HEIGHT; y++)
	{
		for (int layer = 0; layer < layers; layer++)
		{
			const uint16_t *const regs = cave_board.layer_regs[layer];
			/* row-scroll (a line's own x scroll, added) and row-select (the map's line a screen line shows), from
			   the layer's line RAM (tmap038's lineram, 0x1000 bytes into its RAM: two words a line), read at the
			   line sy - 1 + y (cave_v.cpp's m_row_effect_offs_n) */
			int line_x = sx[layer], line_y = y + sy[layer];

			if (!on[layer])
				continue;
			if (row_effects[layer] && (regs[0] & 0x4000u || regs[1] & 0x4000u))
			{
				const uint16_t *const line =
					&cave_board.layer_ram[layer][0x800u + ((unsigned)(sy[layer] - 1 + y) & 0x1FFu) * 2u];

				if (regs[0] & 0x4000u)
					line_x += line[0];
				if (regs[1] & 0x4000u)
					line_y = line[1];
			}
			fetch_line(layer, line_x, (unsigned)line_y, &lines[layer]);
		}
		if (planes)
		{
			/* (each layer's pixels in its own plane, keyed: video_compose picks among them) */
			for (int layer = 0; layer < layers; layer++)
			{
				uint32_t *const out = planes[layer] + y * VIDEO_WIDTH;

				if (!on[layer])
				{
					memset(out, 0, VIDEO_WIDTH * sizeof(uint32_t));
					continue;
				}
				for (int x = 0; x < VIDEO_WIDTH; x++)
				{
					const unsigned palette = lines[layer].palette[x], pen = lines[layer].pen[x];

					out[x] = pen ? (uint32_t)(1 + lines[layer].tile_priority[x] * 16 + layer_priority[layer] * 4
						+ layer) << 24 | colour(VIDEO->layer_colour_base
						+ (VIDEO->eight_bits >> layer & 1u ? palette * 256u : palette * colour_step()) + pen) : 0u;
				}
			}
			continue;
		}
		for (int x = 0; x < VIDEO_WIDTH; x++)
		{
			int best = -1, best_key = -1;

			for (int layer = 0; layer < layers; layer++)
				if (on[layer] && lines[layer].pen[x])
				{
					const int key = lines[layer].tile_priority[x] * 16 + layer_priority[layer] * 4 + layer;

					if (key > best_key)
					{
						best_key = key;
						best = layer;
					}
				}
			if (best >= 0)
			{
				const unsigned palette = lines[best].palette[x], pen = lines[best].pen[x];

				frame[y * VIDEO_WIDTH + x] = colour(VIDEO->layer_colour_base
					+ (VIDEO->eight_bits >> best & 1u ? palette * 256u : palette * colour_step()) + pen);
				priority[y][x] = (uint8_t)(lines[best].tile_priority[x] + 1u);
			}
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
		{
			/* (the rows and columns on the screen only) */
			const int row0 = y < 0 ? -y : 0, row1 = y + height > VIDEO_HEIGHT ? VIDEO_HEIGHT - y : height;
			const int column0 = x < 0 ? -x : 0, column1 = x + width > VIDEO_WIDTH ? VIDEO_WIDTH - x : width;

			for (int row = row0; row < row1; row++)
			{
				const int src_y = flipy ? height - 1 - row : row;
				const uint32_t line = (code << 8) + (uint32_t)(src_y * width);
				uint16_t *const out = &sprite_layer[y + row][x];

				for (int column = column0; column < column1; column++)
				{
					const unsigned pen = sprite_pen(line + (uint32_t)(flipx ? width - 1 - column : column));

					if (pen)
						out[column] = (uint16_t)(pen | colpri);
				}
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
	draw_layers(frame, NULL);
	draw_sprites(frame);
}

int video_draw_planes(uint32_t *planes[VIDEO_PLANES], uint32_t *background)
{
	const int layers = VIDEO->layers;
	uint32_t *const sprites = planes[layers];

	take_sizes();
	*background = colour(VIDEO->background_pen);
	draw_layers(NULL, planes);
	for (int y = 0; y < VIDEO_HEIGHT; y++)
		for (int x = 0; x < VIDEO_WIDTH; x++)
		{
			const uint16_t pixel = sprite_layer[y][x];
			const unsigned colpri = pixel >> 8;

			sprites[y * VIDEO_WIDTH + x] = pixel ? (uint32_t)(1u + (colpri >> 6 & 3u)) << 24
				| colour((pixel & 0xFFu) + (colpri & 0x3Fu) * colour_step()) : 0u;
		}
	return layers + 1;
}

void video_compose(const uint32_t *const *planes, int count, uint32_t background, size_t pixels, uint32_t *out)
{
	const uint32_t *const sprites = planes[count - 1];

	for (size_t at = 0; at < pixels; at++)
	{
		uint32_t best = 0;
		unsigned tile_priority = 0;    /* (0 none: the background; else the tile's priority + 1) */

		for (int layer = 0; layer < count - 1; layer++)
			if (planes[layer][at] > best)    /* (the key in the top byte: the greatest wins) */
				best = planes[layer][at];
		if (best)
			tile_priority = ((best >> 24) - 1u) / 16u + 1u;
		if (sprites[at] && tile_priority <= (sprites[at] >> 24) - 1u)
			best = sprites[at];
		out[at] = best ? best & 0xFFFFFFu : background;
	}
}

void video_rotated(const uint32_t *frame, uint32_t *picture)
{
	/* (ROT270: the board's top row is the picture's left column, read from the bottom up) */
	for (int y = 0; y < VIDEO_HEIGHT; y++)
		for (int x = 0; x < VIDEO_WIDTH; x++)
			picture[(VIDEO_WIDTH - 1 - x) * VIDEO_HEIGHT + y] = frame[y * VIDEO_WIDTH + x];
}
