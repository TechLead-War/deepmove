#include "movegen.h"
#include "board.h"
#include "tables.h"
#include "types.h"

static const int step[8] = {-8, -7, 1, 9, 8, 7, -1, -9};
static const int rook_dirs[4] = {0, 2, 4, 6};
static const int bishop_dirs[4] = {1, 3, 5, 7};

static int first_blocker(int sq, int dir, U64 occ) {
  int d = step[dir];
  int f = FILE(sq), r = RANK(sq);
  int i;
  if (d == 1 || d == -1) {
    for (i = sq + d; i >= 0 && i < 64 && (i / 8) == r; i += d) {
      if (occ & (1ULL << i)) return i;
    }
  } else if (d == 8 || d == -8) {
    for (i = sq + d; i >= 0 && i < 64 && (i % 8) == f; i += d) {
      if (occ & (1ULL << i)) return i;
    }
  } else {
    for (i = sq + d; i >= 0 && i < 64; i += d) {
      int df = FILE(i) - f, dr = RANK(i) - r;
      if (df < -1 || df > 1 || dr < -1 || dr > 1) break;
      if (occ & (1ULL << i)) return i;
      f = FILE(i);
      r = RANK(i);
    }
  }
  return -1;
}

static U64 attackers_to(U64 occ, int sq, int side, U64 pieces[2][6]) {
  U64 att = 0;
  att |= inv_pawn_att[side][sq] & pieces[side][P];
  att |= knight_att[sq] & pieces[side][N];
  att |= king_att[sq] & pieces[side][K];
  for (int i = 0; i < 4; i++) {
    int d = rook_dirs[i];
    int bq = first_blocker(sq, d, occ);
    if (bq >= 0) {
      U64 bb = 1ULL << bq;
      if (bb & (pieces[side][R] | pieces[side][Q])) att |= bb;
    }
  }
  for (int i = 0; i < 4; i++) {
    int d = bishop_dirs[i];
    int bq = first_blocker(sq, d, occ);
    if (bq >= 0) {
      U64 bb = 1ULL << bq;
      if (bb & (pieces[side][BISHOP] | pieces[side][Q])) att |= bb;
    }
  }
  att &= ~(1ULL << sq);
  return att;
}

static int least_attacker_sq(U64 att, int side, U64 pieces[2][6]) {
  U64 bb = att & pieces[side][P];
  if (bb) return POP(bb);
  bb = att & pieces[side][N];
  if (bb) return POP(bb);
  bb = att & pieces[side][BISHOP];
  if (bb) return POP(bb);
  bb = att & pieces[side][R];
  if (bb) return POP(bb);
  bb = att & pieces[side][Q];
  if (bb) return POP(bb);
  bb = att & pieces[side][K];
  if (bb) return POP(bb);
  return -1;
}


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
  if (sq < 0 || sq >= 64) return 0;
  U64 occ = 0;
  int c, p, i;
  int opp = side ^ 1;
  for (c = 0; c < 2; c++) {
    for (p = 0; p < 6; p++) {
      occ |= b->p[c][p];
    }
  }
  if (inv_pawn_att[opp][sq] & b->p[opp][P]) return 1;
  if (knight_att[sq] & b->p[opp][N]) return 1;
  if (king_att[sq] & b->p[opp][K]) return 1;
  for (i = 0; i < 4; i++) {
    int d = rook_dirs[i];
    if (slide_att(sq, d, occ) & (b->p[opp][R] | b->p[opp][Q])) return 1;
  }
  for (i = 0; i < 4; i++) {
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
  if (ksq < 0 || ksq > 63) {
    U64 kbb = b->p[stm][K];
    ksq = kbb ? POP(kbb) : -1;
  }
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
        int d = dirs[i];
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
    int rfrom_k = stm ? 63 : 7;
    int rfrom_q = stm ? 56 : 0;
    if (ksq >= 0 && (b->castle & (stm ? 4 : 1)) && (b->p[stm][R] & (1ULL << rfrom_k)) &&
        !(occ_all & (stm ? 0x6000000000000000ULL : 0x60ULL)) &&
        !is_attacked(b, ksq, stm) && !is_attacked(b, stm ? 62 : 6, stm) && !is_attacked(b, stm ? 61 : 5, stm)) {
      add_move(ml, MOVE(ksq, stm ? 62 : 6, M_CASTLE));
    }
    if (ksq >= 0 && (b->castle & (stm ? 8 : 2)) && (b->p[stm][R] & (1ULL << rfrom_q)) &&
        !(occ_all & (stm ? 0x0E00000000000000ULL : 0x0EULL)) &&
        !is_attacked(b, ksq, stm) && !is_attacked(b, stm ? 58 : 2, stm) && !is_attacked(b, stm ? 59 : 3, stm)) {
      add_move(ml, MOVE(ksq, stm ? 58 : 2, M_CASTLE));
    }
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
  if (ksq < 0 || ksq > 63 || b->piece_on[ksq] != stm * 6 + K) {
    U64 kbb = b->p[stm][K];
    ksq = kbb ? POP(kbb) : -1;
    if (ksq < 0) return 0;
  }
  int piece = b->piece_on[from];
  if (piece < 0 || (piece / 6) != stm) {
    int pc;
    piece = -1;
    for (pc = 0; pc < 6; pc++) {
      if ((b->p[stm][pc] >> from) & 1) {
        piece = stm * 6 + pc;
        break;
      }
    }
    if (piece < 0) return 0;
  }
  int pc = piece % 6;
  if (pc == K) {
    int df = FILE(to) - FILE(from);
    if (df > 1 || df < -1) {
      if (fl != M_CASTLE) return 0;
    }
    Board tmp = *b;
    tmp.p[stm][K] ^= (1ULL << from) | (1ULL << to);
    tmp.occ[stm] ^= (1ULL << from) | (1ULL << to);
    tmp.king_sq[stm] = to;
    if (fl == M_CASTLE) {
      int rfrom = (to == 6 || to == 62) ? (to + 1) : (to - 2);
      int rto = (to == 6 || to == 62) ? (to - 1) : (to + 1);
      if (!(tmp.p[stm][R] & (1ULL << rfrom))) return 0;
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
  if (fl != M_PROMO) {
    b->p[stm][pc] ^= to_bb;
    b->p[stm][pc] |= from_bb;
  } else {
    int pr = PROMO_PC(m);
    if (pr == 0) pr = N; else if (pr == 1) pr = BISHOP; else if (pr == 2) pr = R; else pr = Q;
    b->p[stm][pr] ^= to_bb;
    b->p[stm][P] |= from_bb;
  }
  b->occ[stm] ^= to_bb;
  b->occ[stm] |= from_bb;
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
  return legal;
}

int see(const Board *b, Move m) {
  int from = FROM(m), to = TO(m), fl = FLAGS(m);
  int side = b->side;
  int piece = b->piece_on[from];
  if (piece < 0) return 0;
  int pc = piece % 6;
  int cap_pc = -1;
  if (fl == M_EP) {
    cap_pc = P;
  } else {
    int cap = b->piece_on[to];
    if (cap >= 0) cap_pc = cap % 6;
  }
  if (cap_pc < 0 && fl != M_PROMO) return 0;

  int gain[32];
  int depth = 0;
  gain[0] = (cap_pc >= 0) ? piece_val[cap_pc] : 0;

  U64 occ = b->occ[0] | b->occ[1];
  U64 pieces[2][6];
  for (int c = 0; c < 2; c++) for (int p = 0; p < 6; p++) pieces[c][p] = b->p[c][p];

  pieces[side][pc] &= ~(1ULL << from);
  occ &= ~(1ULL << from);
  if (cap_pc >= 0 && fl != M_EP) {
    pieces[side ^ 1][cap_pc] &= ~(1ULL << to);
  } else if (fl == M_EP) {
    int epsq = side == W ? to - 8 : to + 8;
    pieces[side ^ 1][P] &= ~(1ULL << epsq);
    occ &= ~(1ULL << epsq);
  }
  U64 att[2];
  att[0] = attackers_to(occ, to, 0, pieces);
  att[1] = attackers_to(occ, to, 1, pieces);

  side ^= 1;
  for (;;) {
    U64 attackers = att[side];
    if (!attackers) break;
    int from_sq = least_attacker_sq(attackers, side, pieces);
    if (from_sq < 0) break;
    int attacker_pc = -1;
    for (int p = 0; p < 6; p++) {
      if (pieces[side][p] & (1ULL << from_sq)) { attacker_pc = p; break; }
    }
    if (attacker_pc < 0) break;
    depth++;
    gain[depth] = piece_val[attacker_pc] - gain[depth - 1];
    {
      int a = -gain[depth - 1];
      int b = gain[depth];
      int mx = a > b ? a : b;
      if (mx < 0) break;
    }
    pieces[side][attacker_pc] &= ~(1ULL << from_sq);
    occ &= ~(1ULL << from_sq);
    att[0] = attackers_to(occ, to, 0, pieces);
    att[1] = attackers_to(occ, to, 1, pieces);
    side ^= 1;
  }
  for (int i = depth - 1; i >= 0; i--) {
    int alt = -gain[i + 1];
    if (alt > gain[i]) gain[i] = alt;
  }
  return gain[0];
}
