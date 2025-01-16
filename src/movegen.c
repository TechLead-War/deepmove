#include "movegen.h"
#include "board.h"
#include "tables.h"
#include "types.h"

static const int step[8] = {-8, -7, 1, 9, 8, 7, -1, -9};
static const int rook_dirs[4] = {0, 2, 4, 6};
static const int bishop_dirs[4] = {1, 3, 5, 7};

static inline U64 slide_att(int sq, int dir, U64 occ) {
  U64 att = 0;
  int d = step[dir];
  int f = FILE(sq), r = RANK(sq);
  int i;
  if (d == 1 || d == -1) {
    for (i = sq + d; i >= 0 && i < 64 && (i / 8) == r; i += d) {
      att |= 1ULL << i;
      if (occ & (1ULL << i)) break;
    }
  } else if (d == 8 || d == -8) {
    for (i = sq + d; i >= 0 && i < 64 && (i % 8) == f; i += d) {
      att |= 1ULL << i;
      if (occ & (1ULL << i)) break;
    }
  } else {
    for (i = sq + d; i >= 0 && i < 64; i += d) {
      int df = FILE(i) - f, dr = RANK(i) - r;
      if (df < -1 || df > 1 || dr < -1 || dr > 1) break;
      att |= 1ULL << i;
      if (occ & (1ULL << i)) break;
      f = FILE(i);
      r = RANK(i);
    }
  }
  return att;
}

static void add_move(MoveList *ml, Move m) {
  if (ml->n < MAX_MOVES) ml->m[ml->n++] = m;
}

int is_attacked(const Board *b, int sq, int side) {
  U64 occ = b->occ[0] | b->occ[1];
  int opp = side ^ 1;
  if (inv_pawn_att[opp][sq] & b->p[opp][P]) return 1;
  if (knight_att[sq] & b->p[opp][N]) return 1;
  if (king_att[sq] & b->p[opp][K]) return 1;
  for (int i = 0; i < 4; i++) {
    int d = rook_dirs[i];
    if (slide_att(sq, d, occ) & (b->p[opp][R] | b->p[opp][Q])) return 1;
  }
  for (int i = 0; i < 4; i++) {
    int d = bishop_dirs[i];
    if (slide_att(sq, d, occ) & (b->p[opp][BISHOP] | b->p[opp][Q])) return 1;
  }
  return 0;
}

void gen_moves(const Board *b, MoveList *ml) {
  ml->n = 0;
  int stm = b->side;
  int opp = stm ^ 1;
  U64 occ_all = b->occ[0] | b->occ[1];
  U64 empty = ~occ_all;
  U64 opp_pieces = b->occ[opp];
  int ksq = b->king_sq[stm];
  U64 p, to_bb;
  int from, to;
  p = b->p[stm][P];
  while (p) {
    from = POP(p);
    p &= p - 1;
    to_bb = (pawn_push[stm][from] & empty);
    if (stm == W) {
      if (to_bb & (1ULL << (from + 8))) add_move(ml, MOVE(from, from + 8, M_NORMAL));
      if ((to_bb & (1ULL << (from + 16))) && (empty & (1ULL << (from + 8)))) add_move(ml, MOVE(from, from + 16, M_NORMAL));
    } else {
      if (to_bb & (1ULL << (from - 8))) add_move(ml, MOVE(from, from - 8, M_NORMAL));
      if ((to_bb & (1ULL << (from - 16))) && (empty & (1ULL << (from - 8)))) add_move(ml, MOVE(from, from - 16, M_NORMAL));
    }
    to_bb = pawn_att[stm][from] & opp_pieces;
    while (to_bb) { to = POP(to_bb); to_bb &= to_bb - 1; if (RANK(to) == (stm ? 0 : 7)) for (int pr = N; pr <= Q; pr++) add_move(ml, MOVE(from, to, M_PROMO) | ((pr - 1) << 14)); else add_move(ml, MOVE(from, to, M_NORMAL)); }
    if (b->ep >= 0 && (pawn_att[stm][from] & (1ULL << b->ep))) add_move(ml, MOVE(from, b->ep, M_EP));
    if (RANK(from) == (stm ? 1 : 6)) {
      to_bb = pawn_push[stm][from] & empty;
      while (to_bb) { to = POP(to_bb); to_bb &= to_bb - 1; for (int pr = N; pr <= Q; pr++) add_move(ml, MOVE(from, to, M_PROMO) | ((pr - 1) << 14)); }
    }
  }
  p = b->p[stm][N];
  while (p) {
    from = POP(p);
    p &= p - 1;
    to_bb = knight_att[from] & ~b->occ[stm];
    while (to_bb) { to = POP(to_bb); to_bb &= to_bb - 1; add_move(ml, MOVE(from, to, M_NORMAL)); }
  }
  for (int pc = BISHOP; pc <= Q; pc++) {
    int ndir = (pc == BISHOP) ? 4 : (pc == R) ? 4 : 8;
    const int *dirs = (pc == BISHOP) ? bishop_dirs : (pc == R) ? rook_dirs : (const int[]){0,1,2,3,4,5,6,7};
    p = b->p[stm][pc];
    while (p) {
      from = POP(p);
      p &= p - 1;
      for (int i = 0; i < ndir; i++) {
        int d = (pc == Q) ? dirs[i] : dirs[i];
        to_bb = slide_att(from, d, occ_all) & ~b->occ[stm];
        while (to_bb) { to = POP(to_bb); to_bb &= to_bb - 1; add_move(ml, MOVE(from, to, M_NORMAL)); }
      }
    }
  }
  p = b->p[stm][K];
  if (p) {
    from = POP(p);
    to_bb = king_att[from] & ~b->occ[stm];
    while (to_bb) { to = POP(to_bb); to_bb &= to_bb - 1; add_move(ml, MOVE(from, to, M_NORMAL)); }
    if (b->castle & (stm ? 4 : 1) && !(occ_all & (stm ? 0x6000000000000000ULL : 0x60ULL)) && !is_attacked(b, ksq, stm) && !is_attacked(b, stm ? 62 : 6, stm) && !is_attacked(b, stm ? 61 : 5, stm))
      add_move(ml, MOVE(ksq, stm ? 62 : 6, M_CASTLE));
    if (b->castle & (stm ? 8 : 2) && !(occ_all & (stm ? 0x0E00000000000000ULL : 0x0EULL)) && !is_attacked(b, ksq, stm) && !is_attacked(b, stm ? 58 : 2, stm) && !is_attacked(b, stm ? 59 : 3, stm))
      add_move(ml, MOVE(ksq, stm ? 58 : 2, M_CASTLE));
  }
}

void gen_captures(const Board *b, MoveList *ml) {
  gen_moves(b, ml);
  int i, j = 0;
  for (i = 0; i < ml->n; i++) {
    Move m = ml->m[i];
    int to = TO(m);
    int cap = b->piece_on[to];
    int fl = FLAGS(m);
    if (cap >= 0 || fl == M_EP) ml->m[j++] = m;
  }
  ml->n = j;
}

int move_is_legal(Board *b, Move m) {
  int from = FROM(m), to = TO(m), fl = FLAGS(m);
  int stm = b->side;
  int ksq = b->king_sq[stm];
  int piece = b->piece_on[from];
  if (piece < 0) return 0;
  int pc = piece % 6;
  if (pc == K) {
    Board tmp = *b;
    tmp.p[stm][K] ^= (1ULL << from) | (1ULL << to);
    tmp.occ[stm] ^= (1ULL << from) | (1ULL << to);
    tmp.king_sq[stm] = to;
    if (fl == M_CASTLE) {
      int rfrom = (to == 6 || to == 62) ? (to + 1) : (to - 2);
      int rto = (to == 6 || to == 62) ? (to - 1) : (to + 1);
      tmp.p[stm][R] ^= (1ULL << rfrom) | (1ULL << rto);
      tmp.occ[stm] ^= (1ULL << rfrom) | (1ULL << rto);
    }
    return !is_attacked(&tmp, to, stm);
  }
  U64 from_bb = 1ULL << from, to_bb = 1ULL << to;
  b->p[stm][pc] ^= from_bb; b->p[stm][pc] |= to_bb;
  b->occ[stm] ^= from_bb; b->occ[stm] |= to_bb;
  b->piece_on[from] = -1;
  int cap = b->piece_on[to];
  if (cap >= 0) {
    int c = cap / 6, p = cap % 6;
    b->p[c][p] ^= to_bb;
    b->occ[c] ^= to_bb;
  }
  b->piece_on[to] = piece;
  if (fl == M_EP) {
    int epsq = stm == W ? to - 8 : to + 8;
    b->p[stm^1][P] ^= (1ULL << epsq);
    b->occ[stm^1] ^= (1ULL << epsq);
    b->piece_on[epsq] = -1;
  }
  if (fl == M_PROMO) {
    int pr = PROMO_PC(m) + 1;
    if (pr == 1) pr = N; else if (pr == 2) pr = BISHOP; else if (pr == 3) pr = R; else pr = Q;
    b->p[stm][P] ^= to_bb;
    b->p[stm][pr] |= to_bb;
  }
  int legal = !is_attacked(b, ksq, stm);
  b->p[stm][pc] ^= to_bb; b->p[stm][pc] |= from_bb;
  b->occ[stm] ^= to_bb; b->occ[stm] |= from_bb;
  b->piece_on[from] = piece;
  b->piece_on[to] = cap;
  if (cap >= 0) {
    int c = cap / 6, p = cap % 6;
    b->p[c][p] |= to_bb;
    b->occ[c] |= to_bb;
  }
  if (fl == M_EP) {
    int epsq = stm == W ? to - 8 : to + 8;
    b->p[stm^1][P] |= (1ULL << epsq);
    b->occ[stm^1] |= (1ULL << epsq);
    b->piece_on[epsq] = (stm^1) * 6 + P;
  }
  if (fl == M_PROMO) {
    int pr = PROMO_PC(m) + 1;
    if (pr == 1) pr = N; else if (pr == 2) pr = BISHOP; else if (pr == 3) pr = R; else pr = Q;
    b->p[stm][P] |= to_bb;
    b->p[stm][pr] ^= to_bb;
  }
  return legal;
}
