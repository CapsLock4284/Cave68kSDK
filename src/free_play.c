/**
 *    Name:        free_play.c
 *    Description: free play for home (cave/free_play.h): Start drops in the coins a game needs first
 *    Version:     0.1.0 (draft)
 *    Created:     09-11-2026
 *
 *    license:BSD-3-Clause
 *    The game's credits are where its board description says (cave_credits_desc: cave/board.h). The coins are held
 *    for 6 frames, as MAME's PORT_IMPULSE(6) on the coin inputs.
 */
#include "cave/free_play.h"
#include "cave/board.h"
#include "cave/machine.h"

#define COIN_FRAMES   6     /* a coin held down (MAME's PORT_IMPULSE(6)) */
#define GAP_FRAMES    6     /* let go before the next coin, or the start: the game counts the credit meanwhile */
#define START_FRAMES  6     /* the start the port presses, then lets go as long again, until the game takes it */
#define START_WAIT    300   /* frames the port presses Start for at most (about 5 s): a game may take a start only
                               some time after the credit (DoDonPachi: up to 150 frames on its title, none on its
                               warning screen); after that the credit stays for the player's next Start */
#define MOST_COINS    4     /* a press drops at most these: the game may not take coins just then (its test mode) */

enum { IDLE, COIN, GAP, START };

static struct
{
	int phase;
	int frames;
	int coins;
	unsigned credits;       /* the credits when the start began: taken once fewer */
	bool start_held;
} players[2];

static unsigned credits(void)
{
	return *(const uint16_t *)cave_work_ram_at(cave_board_description->credits->credits, 2u);
}

static unsigned credits_a_game_needs(void)
{
	const cave_credits_desc *const c = cave_board_description->credits;

	return (*cave_work_ram_byte(c->two_credits) >> c->two_credits_bit & 1u) ? 2u : 1u;
}

void free_play_reset(void)
{
	for (int player = 0; player < 2; player++)
	{
		players[player].phase = IDLE;
		players[player].start_held = false;
	}
}

void free_play_inputs(void)
{
	uint16_t *const ports[2] = { &cave_board.in0, &cave_board.in1 };

	if (!cave_board_description->credits)
		return;

	for (int player = 0; player < 2; player++)
	{
		uint16_t *const port = ports[player];
		const bool start = (*port & CAVE_START) == 0;    /* (active low) */
		const bool pressed = start && !players[player].start_held;

		players[player].start_held = start;
		switch (players[player].phase)
		{
		case IDLE:
			if (pressed && credits() < credits_a_game_needs())
			{
				players[player].phase = COIN;
				players[player].frames = COIN_FRAMES;
				players[player].coins = 1;
			}
			else
				break;
			/* fall through */
		case COIN:
			*port = (uint16_t)((*port & ~CAVE_COIN) | CAVE_START);    /* the coin in, the start not yet */
			if (--players[player].frames == 0)
			{
				players[player].phase = GAP;
				players[player].frames = GAP_FRAMES;
			}
			break;
		case GAP:
			*port |= CAVE_START;
			if (--players[player].frames == 0)
			{
				if (credits() < credits_a_game_needs() && players[player].coins < MOST_COINS)
				{
					players[player].phase = COIN;
					players[player].frames = COIN_FRAMES;
					players[player].coins++;
				}
				else
				{
					players[player].phase = START;
					players[player].frames = 0;
					players[player].credits = credits();
				}
			}
			break;
		case START:
			if (credits() < players[player].credits || players[player].frames >= START_WAIT)
			{
				players[player].phase = IDLE;    /* (taken, or the credit left for the player's next Start) */
				break;
			}
			if (players[player].frames++ % (2 * START_FRAMES) < START_FRAMES)
				*port &= (uint16_t)~CAVE_START;
			else
				*port |= CAVE_START;
			break;
		}
	}
}
