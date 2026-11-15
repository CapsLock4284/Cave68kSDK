/**
 *    Name:        boards.h
 *    Description: the games' boards the SDK describes (cave/board.h): one cave_board_desc each
 *    Version:     0.1.0 (draft)
 *    Created:     15-11-2026
 *
 *    license:BSD-3-Clause
 */
#ifndef CAVE_BOARDS_H
#define CAVE_BOARDS_H

#include "cave/board.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DonPachi (Cave, AT-C01DP-2; MAME's donpachi and its clones): src/boards/donpachi.c */
extern const cave_board_desc cave_board_donpachi;

#ifdef __cplusplus
}
#endif

#endif
