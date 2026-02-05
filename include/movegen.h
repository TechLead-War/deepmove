#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "types.h"

int is_attacked(const Board *b, int sq, int side);
void gen_moves(const Board *b, MoveList *ml);
void gen_captures(const Board *b, MoveList *ml);
int move_is_legal(Board *b, Move m);
int see(const Board *b, Move m);

#endif
