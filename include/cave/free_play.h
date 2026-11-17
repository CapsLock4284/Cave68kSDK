/**
 *    Name:        free_play.h
 *    Description: free play for home: Start drops in the coins a game needs first. DonPachi and DoDonPachi have no free
 *                 play setting (their test modes' coin settings start at 1 coin 1 play), so the port plays the coins
 *                 in: only the inputs change, the game runs as it does on the board (coin sound, credit, start). Where
 *                 each game keeps its credits: its board description (cave_credits_desc: cave/board.h)
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 */
#ifndef CAVE_FREE_PLAY_H
#define CAVE_FREE_PLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* After the port has set the inputs (cave_board.in0, in1) and before it runs the frame: a player's Start, pressed
   with fewer credits than a game needs, becomes a coin in that player's slot (held as MAME's 6 frames), then, the
   credit counted, Start again */
void free_play_inputs(void);

/* Forgets a coin or start under way (the game reset) */
void free_play_reset(void);

#ifdef __cplusplus
}
#endif

#endif
