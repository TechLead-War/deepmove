#include "eval.h"
#include "movegen.h"
#include "params.h"
#include "tables.h"
#include "types.h"

static inline int popcount(U64 x) {
#if defined(_MSC_VER)
  return (int)__popcnt64(x);
#else
  return (int)__builtin_popcountll(x);
#endif
}

static int eval_pawn_structure(const Board *b, int c) {
  int score = 0, sq;
  U64 pawns = b->p[c][P];
  U64 opp_pawns = b->p[c ^ 1][P];
  for (int f = 0; f < 8; f++) {
    U64 file_mask = 0x0101010101010101ULL << f;
    int n = 0;
    U64 p = pawns & file_mask;
    while (p) { n++; p &= p - 1; }
    if (n >= 2) score -= 24;
    U64 adj_files = (f > 0 ? (0x0101010101010101ULL << (f - 1)) : 0) | (f < 7 ? (0x0101010101010101ULL << (f + 1)) : 0);
    if (n >= 1 && !(pawns & adj_files)) score -= 12;
  }
  U64 p2 = pawns;
  while (p2) {
    sq = POP(p2);
    p2 &= p2 - 1;
    int r = RANK(sq), fl = FILE(sq);
    U64 front = 0;
    if (c == W) {
      for (int rr = r + 1; rr <= 7; rr++) {
        front |= 1ULL << SQ(fl, rr);
        if (fl > 0) front |= 1ULL << SQ(fl - 1, rr);
        if (fl < 7) front |= 1ULL << SQ(fl + 1, rr);
      }
    } else {
      for (int rr = r - 1; rr >= 0; rr--) {
        front |= 1ULL << SQ(fl, rr);
        if (fl > 0) front |= 1ULL << SQ(fl - 1, rr);
        if (fl < 7) front |= 1ULL << SQ(fl + 1, rr);
      }
    }
    if (!(opp_pawns & front)) {
      int dist = c == W ? (7 - r) : r;
      score += 18 + dist * 10;
    }
  }
  return c == W ? score : -score;
}

static int eval_king_safety(const Board *b, int c) {
  int ksq = b->king_sq[c];
  if (ksq < 0 || ksq > 63) {
    U64 kbb = b->p[c][K];
    if (!kbb) return 0;
    ksq = POP(kbb);
  }
  int f = FILE(ksq), r = RANK(ksq);
  int shield = 0;
  U64 pawns = b->p[c][P];
  int rank1 = c == W ? 1 : 6, rank2 = c == W ? 2 : 5;
  for (int df = -1; df <= 1; df++) {
    int nf = f + df;
    if (nf < 0 || nf > 7) continue;
    if (pawns & (1ULL << SQ(nf, rank1))) shield += 15;
    if (pawns & (1ULL << SQ(nf, rank2))) shield += 8;
  }
  U64 file_bb = 0x0101010101010101ULL << f;
  int open = (b->p[0][P] & file_bb) || (b->p[1][P] & file_bb) ? 0 : 1;
  int pen = -shield + (open ? 25 : 0);
  if (r == (c == W ? 0 : 7)) pen -= 10;
  return c == W ? pen : -pen;
}

static int eval_bishop_pair(const Board *b, int c) {
  if (popcount(b->p[c][BISHOP]) >= 2) return c == W ? 30 : -30;
  return 0;
}

static int eval_rook_activity(const Board *b, int c) {
  int score = 0;
  U64 rooks = b->p[c][R];
  while (rooks) {
    int sq = POP(rooks);
    rooks &= rooks - 1;
    int f = FILE(sq);
    U64 file_mask = 0x0101010101010101ULL << f;
    int own_pawn = (b->p[c][P] & file_mask) != 0;
    int opp_pawn = (b->p[c ^ 1][P] & file_mask) != 0;
    if (!own_pawn && !opp_pawn) score += 20;
    else if (!own_pawn && opp_pawn) score += 12;
    int rank = RANK(sq);
    if ((c == W && rank == 6) || (c == B && rank == 1)) score += 10;
  }
  return c == W ? score : -score;
}

static int eval_king_attack(const Board *b) {
  Board tmp = *b;
  MoveList ml;
  int score = 0;
  for (int c = 0; c < 2; c++) {
    int enemy = c ^ 1;
    int ksq = b->king_sq[enemy];
    if (ksq < 0 || ksq > 63) continue;
    U64 zone = king_att[ksq] | (1ULL << ksq);
    tmp.side = c;
    gen_moves(&tmp, &ml);
    int attacks = 0;
    for (int i = 0; i < ml.n; i++) {
      if (zone & (1ULL << TO(ml.m[i]))) attacks++;
    }
    int bonus = attacks * PARAM_KING_ATTACK_WEIGHT;
    score += (c == W) ? bonus : -bonus;
  }
  return score;
}

static int eval_mobility(const Board *b) {
  Board tmp = *b;
  MoveList ml;
  int score = 0;
  tmp.side = W;
  gen_moves(&tmp, &ml);
  score += ml.n * PARAM_MOBILITY_WEIGHT;
  tmp.side = B;
  gen_moves(&tmp, &ml);
  score -= ml.n * PARAM_MOBILITY_WEIGHT;
  return score;
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
  for (c = 0; c < 2; c++) score += eval_pawn_structure(b, c);
  for (c = 0; c < 2; c++) score += eval_king_safety(b, c);
  for (c = 0; c < 2; c++) score += eval_bishop_pair(b, c);
  for (c = 0; c < 2; c++) score += eval_rook_activity(b, c);
  score += eval_king_attack(b);
  score += eval_mobility(b);
  if (b->side == B) score = -score;
  return score;
}
