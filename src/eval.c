#include "eval.h"
#include "movegen.h"
#include "tables.h"
#include "types.h"

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
  if (ksq < 0 || ksq > 63) return 0;
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
  MoveList ml;
  gen_moves(b, &ml);
  score += (b->side == W ? ml.n : -ml.n) * 6;
  if (b->side == B) score = -score;
  return score;
}
