#ifndef CHESS_H
#define CHESS_H

#include <stdint.h>

#define W 0
#define B 1
#define P 0
#define N 1
#define BISHOP 2
#define R 3
#define Q 4
#define K 5
#define N_PIECES 6
#define MAX_MOVES 256
#define INF 30000
#define MATE 29000
#define MAX_DEPTH 64
#define HASH_SIZE 65536
#define HASH_MASK (HASH_SIZE - 1)

typedef uint64_t U64;
typedef uint16_t Move;

typedef struct {
  U64 p[2][6];
  U64 occ[2];
  int side;
  int castle;
  int ep;
  int fifty;
  int ply;
  U64 key;
  int piece_on[64];
  int king_sq[2];
} Board;

typedef struct {
  Move m[MAX_MOVES];
  int n;
} MoveList;

typedef struct {
  U64 key;
  int depth;
  int flag;
  int score;
  Move best;
} HashEntry;

extern U64 knight_att[64];
extern U64 king_att[64];
extern U64 pawn_push[2][64];
extern U64 pawn_att[2][64];
extern U64 ray_att[64][8];
extern int ray_dir[64][64];
extern int pst[2][6][64];
extern int piece_val[6];
extern HashEntry tt[HASH_SIZE];

void init_tables(void);
void board_from_fen(Board *b, const char *fen);
void board_reset(Board *b);
int is_attacked(const Board *b, int sq, int side);
void gen_moves(const Board *b, MoveList *ml);
void gen_captures(const Board *b, MoveList *ml);
int move_is_legal(Board *b, Move m);
int make_move(Board *b, Move m);
void unmake_move(Board *b, Move m);
int eval(const Board *b);
Move search(Board *b, int depth, int *score);
const char *move_to_uci(Move m);
int uci_to_move(const Board *b, const char *uci, Move *out);

#endif
