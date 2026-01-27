#include "chess.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const int step[8] = {-8, -7, 1, 9, 8, 7, -1, -9};
static const int rook_dirs[4] = {0, 2, 4, 6};
static const int bishop_dirs[4] = {1, 3, 5, 7};

U64 knight_att[64];
U64 king_att[64];
U64 pawn_push[2][64];
U64 pawn_att[2][64];
U64 ray_att[64][8];
int ray_dir[64][64];
int pst[2][6][64];
int piece_val[6];
HashEntry tt[HASH_SIZE];

typedef struct { int castle; int ep; int cap; } Hist;
static Hist hist[MAX_DEPTH];
static int hply;

static U64 inv_pawn_att[2][64];

#define SQ(f,r) ((r)*8+(f))
#define FILE(s) ((s)&7)
#define RANK(s) ((s)>>3)
#define POP(b) __builtin_ctzll(b)
#define POPCNT(b) __builtin_popcountll(b)
#define LSB(b) ((b)&-(long long)(b))
#define MSB(b) (1ULL<<(63-__builtin_clzll(b)))

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

static void init_rays(void) {
  int sq, dir, to;
  for (sq = 0; sq < 64; sq++) {
    for (dir = 0; dir < 8; dir++) {
      U64 r = 0;
      int d = step[dir];
      int f = FILE(sq), rank = RANK(sq);
      if (d == 1 || d == -1) {
        for (to = sq + d; to >= 0 && to < 64 && (to / 8) == rank; to += d) r |= 1ULL << to;
      } else if (d == 8 || d == -8) {
        for (to = sq + d; to >= 0 && to < 64 && (to % 8) == f; to += d) r |= 1ULL << to;
      } else {
        for (to = sq + d; to >= 0 && to < 64; to += d) {
          int df = FILE(to) - f, dr = RANK(to) - rank;
          if (df < -1 || df > 1 || dr < -1 || dr > 1) break;
          r |= 1ULL << to;
          f = FILE(to);
          rank = RANK(to);
        }
      }
      ray_att[sq][dir] = r;
    }
    for (to = 0; to < 64; to++) ray_dir[sq][to] = -1;
    for (dir = 0; dir < 8; dir++) {
      U64 r = ray_att[sq][dir];
      while (r) { to = POP(r); r &= r - 1; ray_dir[sq][to] = dir; }
    }
  }
}

void init_tables(void) {
  int sq, i;
  init_rays();
  for (sq = 0; sq < 64; sq++) {
    U64 k = 0;
    int f = FILE(sq), r = RANK(sq);
    for (i = 0; i < 8; i++) {
      int nr = r + (i < 4 ? (i < 2 ? -1 : 1) : 0);
      int nf = f + (i == 0 || i == 4 ? 0 : (i == 1 || i == 3 || i == 5 ? 1 : -1));
      if (i == 0) nf = f - 1;
      if (i == 1) { nf = f + 1; nr = r - 1; }
      if (i == 2) { nf = f + 1; nr = r; }
      if (i == 3) { nf = f + 1; nr = r + 1; }
      if (i == 4) nr = r + 1;
      if (i == 5) { nf = f - 1; nr = r + 1; }
      if (i == 6) nf = f - 1;
      if (i == 7) { nf = f - 1; nr = r - 1; }
      if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) k |= 1ULL << SQ(nf, nr);
    }
    king_att[sq] = k;
    k = 0;
    int dn[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for (i = 0; i < 8; i++) {
      int nf = f + dn[i][0], nr = r + dn[i][1];
      if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) k |= 1ULL << SQ(nf, nr);
    }
    knight_att[sq] = k;
    pawn_push[W][sq] = (r < 7) ? (1ULL << (sq + 8)) : 0;
    if (r == 1) pawn_push[W][sq] |= 1ULL << (sq + 16);
    pawn_push[B][sq] = (r > 0) ? (1ULL << (sq - 8)) : 0;
    if (r == 6) pawn_push[B][sq] |= 1ULL << (sq - 16);
    pawn_att[W][sq] = 0;
    if (r < 7) {
      if (f > 0) pawn_att[W][sq] |= 1ULL << (sq + 7);
      if (f < 7) pawn_att[W][sq] |= 1ULL << (sq + 9);
    }
    pawn_att[B][sq] = 0;
    if (r > 0) {
      if (f > 0) pawn_att[B][sq] |= 1ULL << (sq - 9);
      if (f < 7) pawn_att[B][sq] |= 1ULL << (sq - 7);
    }
    inv_pawn_att[W][sq] = (r >= 1 && f >= 1 ? (1ULL << (sq - 7)) : 0) | (r >= 1 && f <= 6 ? (1ULL << (sq - 9)) : 0);
    inv_pawn_att[B][sq] = (r <= 6 && f <= 6 ? (1ULL << (sq + 7)) : 0) | (r <= 6 && f >= 1 ? (1ULL << (sq + 9)) : 0);
  }
  piece_val[P] = 100;
  piece_val[N] = 320;
  piece_val[BISHOP] = 330;
  piece_val[R] = 500;
  piece_val[Q] = 900;
  piece_val[K] = 0;
  for (sq = 0; sq < 64; sq++) {
    int r = RANK(sq), f = FILE(sq);
    int cr = r < 4 ? r : 7 - r;
    int cf = f < 4 ? f : 7 - f;
    int c = cr + cf;
    pst[W][P][sq] = (r >= 1 && r <= 5) ? (r - 1) * 5 + (f == 3 || f == 4 ? 5 : 0) : (r == 6 ? 25 : 0);
    pst[B][P][sq] = pst[W][P][63 - sq];
    pst[W][N][sq] = c * 3 + (r >= 2 && r <= 5 && f >= 2 && f <= 5 ? 10 : 0);
    pst[B][N][sq] = pst[W][N][63 - sq];
    pst[W][BISHOP][sq] = c * 2 + (f == r || f == 7 - r ? 5 : 0);
    pst[B][BISHOP][sq] = pst[W][BISHOP][63 - sq];
    pst[W][R][sq] = (r == 6 ? 15 : 0) + (f == 0 || f == 7 ? -5 : 0) + (r == 7 ? 5 : 0);
    pst[B][R][sq] = pst[W][R][63 - sq];
    pst[W][Q][sq] = c + (r >= 2 && r <= 5 ? 3 : 0);
    pst[B][Q][sq] = pst[W][Q][63 - sq];
    pst[W][K][sq] = (r == 0 && f >= 2 && f <= 6 ? -20 : 0) + (r >= 1 ? (c * 2) : 0);
    pst[B][K][sq] = pst[W][K][63 - sq];
  }
}

static U64 rand64(void) {
  static U64 s = 0x8a5cd789635d2dffULL;
  s ^= s >> 12;
  s ^= s << 25;
  s ^= s >> 27;
  return s * 2685821657736338717ULL;
}

static U64 zobrist_piece[2][6][64];
static U64 zobrist_side;
static U64 zobrist_ep[8];
static U64 zobrist_castle[4];

static void init_zobrist(void) {
  int c, p, sq;
  for (c = 0; c < 2; c++)
    for (p = 0; p < 6; p++)
      for (sq = 0; sq < 64; sq++)
        zobrist_piece[c][p][sq] = rand64();
  zobrist_side = rand64();
  for (sq = 0; sq < 8; sq++) zobrist_ep[sq] = rand64();
  for (sq = 0; sq < 4; sq++) zobrist_castle[sq] = rand64();
}

static U64 compute_key(const Board *b) {
  U64 k = 0;
  int c, p, sq;
  for (c = 0; c < 2; c++)
    for (p = 0; p < 6; p++) {
      U64 bb = b->p[c][p];
      while (bb) { sq = POP(bb); bb &= bb - 1; k ^= zobrist_piece[c][p][sq]; }
    }
  if (b->side == B) k ^= zobrist_side;
  if (b->ep >= 0 && b->ep < 64) k ^= zobrist_ep[FILE(b->ep)];
  if (b->castle & 1) k ^= zobrist_castle[0];
  if (b->castle & 2) k ^= zobrist_castle[1];
  if (b->castle & 4) k ^= zobrist_castle[2];
  if (b->castle & 8) k ^= zobrist_castle[3];
  return k;
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
  b->p[B][Q] = 0x800000000000000ULL;
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
  if (!zobrist_piece[0][0][1]) init_zobrist();
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
  if (!zobrist_piece[0][0][1]) init_zobrist();
  b->key = compute_key(b);
}

void board_sync(Board *b) {
  int c, p, sq;
  b->occ[W] = b->p[W][P] | b->p[W][N] | b->p[W][BISHOP] | b->p[W][R] | b->p[W][Q] | b->p[W][K];
  b->occ[B] = b->p[B][P] | b->p[B][N] | b->p[B][BISHOP] | b->p[B][R] | b->p[B][Q] | b->p[B][K];
  for (sq = 0; sq < 64; sq++) b->piece_on[sq] = -1;
  for (c = 0; c < 2; c++)
    for (p = 0; p < 6; p++) {
      U64 bb = b->p[c][p];
      while (bb) { sq = POP(bb); bb &= bb - 1; b->piece_on[sq] = c * 6 + p; if (p == K) b->king_sq[c] = sq; }
    }
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

#define MOVE(f,t,fl) ((f)|((t)<<6)|((fl)<<12))
#define FROM(m) ((m)&0x3F)
#define TO(m) (((m)>>6)&0x3F)
#define FLAGS(m) (((m)>>12)&3)
#define PROMO_PC(m) (((m)>>14)&3)

enum { M_NORMAL, M_PROMO, M_EP, M_CASTLE };

static void add_move(MoveList *ml, Move m) {
  if (ml->n < MAX_MOVES) ml->m[ml->n++] = m;
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
  if ((piece / 6) != stm) return 0;
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

int eval(const Board *b) {
  int score = 0;
  int c, p, sq;
  for (c = 0; c < 2; c++) {
    int sign = (c == W) ? 1 : -1;
    for (p = 0; p < 6; p++) {
      U64 bb = b->p[c][p];
      while (bb) {
        sq = POP(bb);
        bb &= bb - 1;
        score += sign * (piece_val[p] + pst[c][p][sq]);
      }
    }
  }
  if (b->side == B) score = -score;
  MoveList ml;
  gen_moves(b, &ml);
  score += ml.n * 4;
  return score;
}

static int quiesce(Board *b, int alpha, int beta) {
  int stand = eval(b);
  if (stand >= beta) return beta;
  if (stand > alpha) alpha = stand;
  MoveList ml;
  gen_captures(b, &ml);
  int best = stand;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (!move_is_legal(b, m)) continue;
    make_move(b, m);
    int score = -quiesce(b, -beta, -alpha);
    unmake_move(b, m);
    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
    if (score > best) best = score;
  }
  return best;
}

static int search_inner(Board *b, int depth, int alpha, int beta, Move *pv_best) {
  if (depth <= 0) return quiesce(b, alpha, beta);
  U64 key = b->key;
  HashEntry *he = &tt[key & HASH_MASK];
  if (he->key == key && he->depth >= depth) {
    if (he->flag == 0) return he->score;
    if (he->flag == 1 && he->score >= beta) return he->score;
    if (he->flag == 2 && he->score <= alpha) return he->score;
  }
  MoveList ml;
  gen_moves(b, &ml);
  if (ml.n == 0) {
    if (is_attacked(b, b->king_sq[b->side], b->side)) return -MATE + b->ply;
    return 0;
  }
  int best = -INF;
  Move best_m = 0;
  if (he->key == key && he->best)
    for (int i = 0; i < ml.n; i++)
      if (ml.m[i] == he->best) { ml.m[i] = ml.m[0]; ml.m[0] = he->best; break; }
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (!move_is_legal(b, m)) continue;
    make_move(b, m);
    int score;
    if (depth <= 1) score = -quiesce(b, -beta, -alpha);
    else score = -search_inner(b, depth - 1, -beta, -alpha, NULL);
    unmake_move(b, m);
    if (score > best) {
      best = score;
      best_m = m;
      if (pv_best) *pv_best = m;
      if (best >= beta) break;
      if (best > alpha) alpha = best;
    }
  }
  he->key = key;
  he->depth = depth;
  he->score = best;
  he->flag = (best >= beta) ? 1 : (best <= alpha) ? 2 : 0;
  he->best = best_m;
  return best;
}

Move search(Board *b, int depth, int *score) {
  hply = 0;
  Move best = 0;
  int alpha = -INF, beta = INF;
  int d;
  for (d = 1; d <= depth; d++) {
    Move pv_move = 0;
    int s = search_inner(b, d, alpha, beta, &pv_move);
    if (pv_move) best = pv_move;
    if (score) *score = s;
    if (s >= MATE - 64 || s <= -MATE + 64) break;
  }
  return best;
}

const char *move_to_uci(Move m) {
  static char buf[8];
  int f = FROM(m), t = TO(m);
  sprintf(buf, "%c%c%c%c", 'a' + FILE(f), '1' + RANK(f), 'a' + FILE(t), '1' + RANK(t));
  if (FLAGS(m) == M_PROMO) {
    int pr = PROMO_PC(m);
    buf[4] = (pr == 0) ? 'n' : (pr == 1) ? 'b' : (pr == 2) ? 'r' : 'q';
    buf[5] = 0;
  }
  return buf;
}

static int parse_squares(const char *uci, int *from, int *to, char *promo) {
  const char *p = uci;
  int sq[2];
  int n = 0;
  *promo = 0;
  while (*p && n < 3) {
    int fc = *p | 32;
    if (fc >= 'a' && fc <= 'h' && p[1] >= '1' && p[1] <= '8') {
      if (n < 2) sq[n++] = (p[1] - '1') * 8 + (fc - 'a');
      p += 2;
      continue;
    }
    if (n == 2 && (*p == 'n' || *p == 'b' || *p == 'r' || *p == 'q' || *p == 'N' || *p == 'B' || *p == 'R' || *p == 'Q')) {
      *promo = (char)(*p | 32);
      p++;
      continue;
    }
    p++;
  }
  if (n < 2) return 0;
  *from = sq[0];
  *to = sq[1];
  return 1;
}

int uci_to_move(const Board *b, const char *uci, Move *out) {
  int from, to;
  char promo;
  if (!uci) return 0;
  while (*uci == ' ' || *uci == '\t') uci++;
  if (!parse_squares(uci, &from, &to, &promo)) return 0;
  board_sync((Board *)b);
  MoveList ml;
  gen_moves(b, &ml);
  int first = -1, count = 0;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (FROM(m) != from || TO(m) != to) continue;
    if (FLAGS(m) == M_PROMO) {
      int prn = (promo == 'n') ? 0 : (promo == 'b') ? 1 : (promo == 'r') ? 2 : (promo == 'q') ? 3 : -1;
      if (prn < 0 || PROMO_PC(m) != prn) continue;
    } else if (promo) continue;
    if (first < 0) first = i;
    count++;
  }
  if (first < 0) return 0;
  if (count == 1) { *out = ml.m[first]; return 1; }
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (FROM(m) != from || TO(m) != to) continue;
    if (FLAGS(m) == M_PROMO) {
      int prn = (promo == 'n') ? 0 : (promo == 'b') ? 1 : (promo == 'r') ? 2 : (promo == 'q') ? 3 : -1;
      if (prn < 0 || PROMO_PC(m) != prn) continue;
    } else if (promo) continue;
    if (move_is_legal((Board *)b, m)) { *out = m; return 1; }
  }
  return 0;
}
