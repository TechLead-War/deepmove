#ifndef BOARD_H
#define BOARD_H

#include "types.h"

void board_reset(Board *b);
void board_from_fen(Board *b, const char *fen);
int make_move(Board *b, Move m);
void unmake_move(Board *b, Move m);
void board_clear_hist(void);

#endif
