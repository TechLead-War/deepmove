#ifndef TABLES_H
#define TABLES_H

#include "types.h"

extern U64 knight_att[64];
extern U64 king_att[64];
extern U64 pawn_push[2][64];
extern U64 pawn_att[2][64];
extern U64 inv_pawn_att[2][64];
extern U64 ray_att[64][8];
extern int ray_dir[64][64];
extern int pst[2][6][64];
extern int piece_val[6];
extern HashEntry tt[HASH_SIZE];

void init_tables(void);
U64 tables_compute_key(const Board *b);
void tables_ensure_zobrist(void);
int tables_zobrist_ready(void);

#endif
