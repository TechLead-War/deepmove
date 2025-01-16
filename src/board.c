#include "board.h"
#include "tables.h"
#include "types.h"
#include <string.h>
#include <stdlib.h>

typedef struct { int castle; int ep; int cap; } Hist;
static Hist hist[MAX_DEPTH];
static int hply;

void board_clear_hist(void) { hply = 0; }
int board_hist_ply(void) { return hply; }

static U64 compute_key(const Board *b) {
  return tables_compute_key(b);
}

void board_reset(Board *b) {
  memset(b, 0, sizeof(Board));
  b->p[W][P] = 0xFF00ULL;
  b->p[W][N] = 0x42ULL;
  b->p[W][BISHOP] = 0x24ULL;
  b->p[W][R] = 0x81ULL;
  b->p[W][Q] = 0x8ULL;
  b->p[W][K] = 0x10ULL;
  b->p[B][P] = 0xFF000000000000ULL;
  b->p[B][N] = 0x4200000000000000ULL;
  b->p[B][BISHOP] = 0x2400000000000000ULL;
  b->p[B][R] = 0x8100000000000000ULL;
  b->p[B][Q] = 0x80000000000000ULL;
  b->p[B][K] = 0x1000000000000000ULL;
  b->occ[W] = 0xFFFFULL;
  b->occ[B] = 0xFFFF000000000000ULL;
  b->side = W;
  b->castle = 15;
  b->ep = -1;
  b->king_sq[W] = 4;
  b->king_sq[B] = 60;
  for (int sq = 0; sq < 64; sq++) b->piece_on[sq] = -1;
  for (int c = 0; c < 2; c++)
    for (int p = 0; p < 6; p++) {
      U64 bb = b->p[c][p];
      while (bb) { int s = POP(bb); bb &= bb - 1; b->piece_on[s] = c * 6 + p; }
    }
  tables_ensure_zobrist();
  b->key = compute_key(b);
}

void board_from_fen(Board *b, const char *fen) {
  memset(b, 0, sizeof(Board));
  int sq = 56;
  const char *s = fen;
  while (*s && sq >= 0 && sq < 64) {
    if (*s == ' ') break;
    if (*s >= '1' && *s <= '8') { sq += (*s - '0'); s++; continue; }
    if (*s == '/') { sq -= 16; s++; continue; }
    int c = (*s >= 'A' && *s <= 'Z') ? W : B;
    int p = -1;
    switch (*s | 32) {
      case 'p': p = P; break;
      case 'n': p = N; break;
      case 'b': p = BISHOP; break;
      case 'r': p = R; break;
      case 'q': p = Q; break;
      case 'k': p = K; break;
    }
    if (p >= 0) {
      b->p[c][p] |= 1ULL << sq;
      b->piece_on[sq] = c * 6 + p;
      if (p == K) b->king_sq[c] = sq;
      sq++;
    }
    s++;
  }
  b->occ[W] = b->p[W][P] | b->p[W][N] | b->p[W][BISHOP] | b->p[W][R] | b->p[W][Q] | b->p[W][K];
  b->occ[B] = b->p[B][P] | b->p[B][N] | b->p[B][BISHOP] | b->p[B][R] | b->p[B][Q] | b->p[B][K];
  while (*s == ' ') s++;
  b->side = (*s == 'w' || *s == 'W') ? W : B;
  s++;
  while (*s == ' ') s++;
  b->castle = 0;
  while (*s && *s != ' ') {
    if (*s == 'K') b->castle |= 1;
    if (*s == 'Q') b->castle |= 2;
    if (*s == 'k') b->castle |= 4;
    if (*s == 'q') b->castle |= 8;
    s++;
  }
  while (*s == ' ') s++;
  b->ep = -1;
  if (*s >= 'a' && *s <= 'h' && s[1] >= '1' && s[1] <= '8') {
    b->ep = (s[1] - '1') * 8 + (*s - 'a');
    s += 2;
  }
  while (*s == ' ') s++;
  b->fifty = 0;
  if (*s >= '0' && *s <= '9') { b->fifty = atoi(s); while (*s >= '0' && *s <= '9') s++; }
  tables_ensure_zobrist();
  b->key = compute_key(b);
}

int make_move(Board *b, Move m) {
  int from = FROM(m), to = TO(m), fl = FLAGS(m);
  int stm = b->side;
  int piece = b->piece_on[from];
  if (piece < 0) return 0;
  int pc = piece % 6;
  hist[hply].castle = b->castle;
  hist[hply].ep = b->ep;
  hist[hply].cap = b->piece_on[to];
  hply++;
  U64 from_bb = 1ULL << from, to_bb = 1ULL << to;
  b->p[stm][pc] ^= from_bb;
  b->occ[stm] ^= from_bb;
  b->piece_on[from] = -1;
  int cap = b->piece_on[to];
  if (cap >= 0) {
    int c = cap / 6, p = cap % 6;
    b->p[c][p] ^= to_bb;
    b->occ[c] ^= to_bb;
  }
  b->piece_on[to] = piece;
  if (pc == P && fl == M_EP) {
    int epsq = stm == W ? to - 8 : to + 8;
    b->p[stm^1][P] ^= (1ULL << epsq);
    b->occ[stm^1] ^= (1ULL << epsq);
    b->piece_on[epsq] = -1;
  }
  if (pc == K) {
    b->king_sq[stm] = to;
    if (fl == M_CASTLE) {
      int rfrom = (to == 6 || to == 62) ? (to + 1) : (to - 2);
      int rto = (to == 6 || to == 62) ? (to - 1) : (to + 1);
      b->p[stm][R] ^= (1ULL << rfrom) | (1ULL << rto);
      b->occ[stm] ^= (1ULL << rfrom) | (1ULL << rto);
      b->piece_on[rfrom] = -1;
      b->piece_on[rto] = stm * 6 + R;
    }
  }
  if (fl == M_PROMO) {
    int pr = PROMO_PC(m);
    if (pr == 0) pr = N; else if (pr == 1) pr = BISHOP; else if (pr == 2) pr = R; else pr = Q;
    b->p[stm][P] ^= to_bb;
    b->p[stm][pr] |= to_bb;
    b->piece_on[to] = stm * 6 + pr;
  } else {
    b->p[stm][pc] |= to_bb;
  }
  b->occ[stm] |= to_bb;
  b->ep = -1;
  if (pc == P && (RANK(to) - RANK(from)) * (stm ? -1 : 1) == 2) b->ep = stm == W ? from + 8 : from - 8;
  if (from == 4) b->castle &= ~3;
  if (from == 60) b->castle &= ~12;
  if (from == 0) b->castle &= ~2;
  if (from == 7) b->castle &= ~1;
  if (from == 56) b->castle &= ~8;
  if (from == 63) b->castle &= ~4;
  b->side ^= 1;
  b->ply++;
  b->key = compute_key(b);
  return 1;
}

void unmake_move(Board *b, Move m) {
  hply--;
  b->side ^= 1;
  b->ply--;
  int from = FROM(m), to = TO(m), fl = FLAGS(m);
  int stm = b->side;
  int piece = b->piece_on[to];
  if (piece < 0) return;
  int pc = piece % 6;
  if (fl == M_PROMO) {
    int pr = PROMO_PC(m);
    if (pr == 0) pr = N; else if (pr == 1) pr = BISHOP; else if (pr == 2) pr = R; else pr = Q;
    b->p[stm][pr] ^= (1ULL << to);
    b->p[stm][P] |= (1ULL << to);
    pc = P;
  }
  U64 from_bb = 1ULL << from, to_bb = 1ULL << to;
  b->p[stm][pc] ^= to_bb;
  b->p[stm][pc] |= from_bb;
  b->occ[stm] ^= to_bb;
  b->occ[stm] |= from_bb;
  b->piece_on[to] = -1;
  b->piece_on[from] = stm * 6 + pc;
  if (pc == K) b->king_sq[stm] = from;
  if (fl == M_CASTLE && pc == K) {
    int rfrom = (to == 6 || to == 62) ? (to + 1) : (to - 2);
    int rto = (to == 6 || to == 62) ? (to - 1) : (to + 1);
    b->p[stm][R] ^= (1ULL << rfrom) | (1ULL << rto);
    b->occ[stm] ^= (1ULL << rfrom) | (1ULL << rto);
    b->piece_on[rto] = -1;
    b->piece_on[rfrom] = stm * 6 + R;
  }
  int cap = hist[hply].cap;
  if (cap >= 0) {
    int c = cap / 6, p = cap % 6;
    b->p[c][p] |= to_bb;
    b->occ[c] |= to_bb;
    b->piece_on[to] = cap;
  } else if (fl == M_EP) {
    int epsq = stm == W ? to - 8 : to + 8;
    b->p[stm^1][P] |= (1ULL << epsq);
    b->occ[stm^1] |= (1ULL << epsq);
    b->piece_on[epsq] = (stm^1) * 6 + P;
  }
  b->castle = hist[hply].castle;
  b->ep = hist[hply].ep;
  b->key = compute_key(b);
}
