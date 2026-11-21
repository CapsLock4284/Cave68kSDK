/**
 *    Name:        video.h
 *    Description: DonPachi's picture: the three NEC 038 tile layers, the 013 sprites and the palette, drawn from
 *                 the board's memories into a 320 x 240 frame
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME (BSD-3-Clause): src/mame/atlus/cave_v.cpp (screen_update, tilemap_draw, colpri_cb), sprite013.cpp
 *    (get_sprite_info, draw_sprites, draw_single_sprite_nozoom, draw) and src/devices/video/tmap038.cpp
 *    (get_tile_info). The screen is turned on its side in the cabinet (MAME's ROT270): the frame is the board's,
 *    320 wide; video_rotated gives the picture as the player sees it, 240 wide.
 */
#ifndef CAVE_VIDEO_H
#define CAVE_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_WIDTH   320
#define VIDEO_HEIGHT  240

/* The sprites, from the sprite RAM's bank the program shows: when VBLANK starts, as the 013 does */
void video_latch_sprites(void);

/* The frame, from the board's memories: 0x00RRGGBB pixels, VIDEO_WIDTH x VIDEO_HEIGHT */
void video_draw(uint32_t *frame);

/* The frame turned as the cabinet shows it: VIDEO_HEIGHT wide, VIDEO_WIDTH high */
void video_rotated(const uint32_t *frame, uint32_t *picture);

/* ---- the picture in planes: each tile layer and the sprites on their own, to be made larger one by one (a pixel-art
   scaler that only picks among a plane's own pixels: no blending) and put together again by the board's rules ----

   A plane's pixel is its colour (0x00RRGGBB) with a key in its top byte, 0 where the plane has nothing (the whole
   pixel 0). The tile layers' key: 1 + the tile's priority * 16 + the layer's priority * 4 + the layer (the greatest
   shows); the sprites' (the last plane): 1 + the sprite's priority (it shows over the tiles of a lower priority) */
#define VIDEO_PLANES  5    /* at most: 4 tile layers, then the sprites */

/* The frame's planes (each VIDEO_WIDTH x VIDEO_HEIGHT, as video_draw's frame): how many (the game's layers + 1, the
   sprites last); 'background' the colour where none has anything. video_compose of them is video_draw's frame */
int video_draw_planes(uint32_t *planes[VIDEO_PLANES], uint32_t *background);

/* The planes ('count', in video_draw_planes' order, any size or turn, 'pixels' each) put together into 'out' as the
   board does (0x00RRGGBB) */
void video_compose(const uint32_t *const *planes, int count, uint32_t background, size_t pixels, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif
