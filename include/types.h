#ifndef TYPES_H
#define TYPES_H

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
#define HASH_SIZE 262144
#define HASH_MASK (HASH_SIZE - 1)
#define HIST_SIZE 1024

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

#define SQ(f,r) ((r)*8+(f))
#define FILE(s) ((s)&7)
#define RANK(s) ((s)>>3)
#define MOVE(f,t,fl) ((f)|((t)<<6)|((fl)<<12))
#define FROM(m) ((m)&0x3F)
#define TO(m) (((m)>>6)&0x3F)
#define FLAGS(m) (((m)>>12)&3)
#define PROMO_PC(m) (((m)>>14)&3)

#define M_NORMAL 0
#define M_PROMO  1
#define M_EP     2
#define M_CASTLE 3

#if defined(_MSC_VER)
#include <intrin.h>
static inline int bit_ctz64(U64 x) {
  unsigned long i;
  return _BitScanForward64(&i, x) ? (int)i : 64;
}
#else
static inline int bit_ctz64(U64 x) {
  return x ? (int)__builtin_ctzll(x) : 64;
}
#endif

#define POP(b) bit_ctz64(b)

#endif
