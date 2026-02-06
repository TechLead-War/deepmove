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

static int eval_pawn_structure_side(const Board *b, int c) {
  int score = 0, sq;
  U64 pawns = b->p[c][P];
  U64 opp_pawns = b->p[c ^ 1][P];
  for (int f = 0; f < 8; f++) {
    U64 file_mask = 0x0101010101010101ULL << f;
    int n = 0;
    U64 p = pawns & file_mask;
    while (p) { n++; p &= p - 1; }
    if (n >= 2) score -= PARAM_PAWN_DOUBLED_PENALTY;
    U64 adj_files = (f > 0 ? (0x0101010101010101ULL << (f - 1)) : 0) | (f < 7 ? (0x0101010101010101ULL << (f + 1)) : 0);
    if (n >= 1 && !(pawns & adj_files)) score -= PARAM_PAWN_ISOLATED_PENALTY;
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
      score += PARAM_PASSED_PAWN_BASE + dist * PARAM_PASSED_PAWN_ADVANCE;
    }
  }
  return c == W ? score : -score;
}

static int eval_king_safety_side(const Board *b, int c) {
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
    if (pawns & (1ULL << SQ(nf, rank1))) shield += PARAM_KING_SHIELD_RANK1;
    if (pawns & (1ULL << SQ(nf, rank2))) shield += PARAM_KING_SHIELD_RANK2;
  }
  U64 file_bb = 0x0101010101010101ULL << f;
  int open = (b->p[0][P] & file_bb) || (b->p[1][P] & file_bb) ? 0 : 1;
  int pen = -shield + (open ? PARAM_KING_OPEN_FILE_PENALTY : 0);
  if (r == (c == W ? 0 : 7)) pen -= PARAM_KING_BACK_RANK_PENALTY;
  return c == W ? pen : -pen;
}

static int eval_bishop_pair_side(const Board *b, int c) {
  if (popcount(b->p[c][BISHOP]) >= 2) return c == W ? PARAM_BISHOP_PAIR_BONUS : -PARAM_BISHOP_PAIR_BONUS;
  return 0;
}

static int eval_rook_activity_side(const Board *b, int c) {
  int score = 0;
  U64 rooks = b->p[c][R];
  while (rooks) {
    int sq = POP(rooks);
    rooks &= rooks - 1;
    int f = FILE(sq);
    U64 file_mask = 0x0101010101010101ULL << f;
    int own_pawn = (b->p[c][P] & file_mask) != 0;
    int opp_pawn = (b->p[c ^ 1][P] & file_mask) != 0;
    if (!own_pawn && !opp_pawn) score += PARAM_ROOK_OPEN_FILE_BONUS;
    else if (!own_pawn && opp_pawn) score += PARAM_ROOK_SEMI_OPEN_BONUS;
    int rank = RANK(sq);
    if ((c == W && rank == 6) || (c == B && rank == 1)) score += PARAM_ROOK_SEVENTH_BONUS;
  }
  return c == W ? score * PARAM_ROOK_ACTIVITY_WEIGHT : -score * PARAM_ROOK_ACTIVITY_WEIGHT;
}

static int attack_weight_for_piece(int pc) {
  if (pc == P) return PARAM_ATTACK_WEIGHT_PAWN;
  if (pc == N || pc == BISHOP) return PARAM_ATTACK_WEIGHT_MINOR;
  if (pc == R) return PARAM_ATTACK_WEIGHT_ROOK;
  if (pc == Q) return PARAM_ATTACK_WEIGHT_QUEEN;
  return 0;
}

static int eval_king_attack(const Board *b) {
  Board tmp = *b;
  MoveList ml;
  int score = 0;
  for (int c = 0; c < 2; c++) {
    int enemy = c ^ 1;
    int ksq = b->king_sq[enemy];
    if (ksq < 0 || ksq > 63) {
      U64 kbb = b->p[enemy][K];
      if (!kbb) continue;
      ksq = POP(kbb);
    }
    U64 zone = king_att[ksq] | (1ULL << ksq);
    tmp.side = c;
    gen_moves(&tmp, &ml);
    int attacks = 0;
    for (int i = 0; i < ml.n; i++) {
      if (!(zone & (1ULL << TO(ml.m[i])))) continue;
      int from = FROM(ml.m[i]);
      int piece = b->piece_on[from];
      if (piece < 0) continue;
      attacks += attack_weight_for_piece(piece % 6);
    }
    int bonus = attacks * PARAM_KING_ATTACK_SCALE;
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
  score += ml.n;
  tmp.side = B;
  gen_moves(&tmp, &ml);
  score -= ml.n;
  return score;
}

static int eval_center_control(const Board *b) {
  static const U64 center_mask =
      (1ULL << SQ(3, 3)) | (1ULL << SQ(4, 3)) | (1ULL << SQ(3, 4)) | (1ULL << SQ(4, 4));
  static const U64 extended_mask =
      (1ULL << SQ(2, 2)) | (1ULL << SQ(3, 2)) | (1ULL << SQ(4, 2)) | (1ULL << SQ(5, 2)) |
      (1ULL << SQ(2, 3)) | (1ULL << SQ(5, 3)) |
      (1ULL << SQ(2, 4)) | (1ULL << SQ(5, 4)) |
      (1ULL << SQ(2, 5)) | (1ULL << SQ(3, 5)) | (1ULL << SQ(4, 5)) | (1ULL << SQ(5, 5)) |
      (1ULL << SQ(2, 6)) | (1ULL << SQ(3, 6)) | (1ULL << SQ(4, 6)) | (1ULL << SQ(5, 6));
  int cw = popcount(b->occ[W] & center_mask);
  int cb = popcount(b->occ[B] & center_mask);
  int ew = popcount(b->occ[W] & extended_mask);
  int eb = popcount(b->occ[B] & extended_mask);
  int score = (cw - cb) * PARAM_CENTER_OCC_BONUS + (ew - eb) * PARAM_CENTER_EXT_BONUS;
  return score;
}

static int eval_phase(const Board *b) {
  int phase = 0;
  phase += popcount(b->p[W][N] | b->p[B][N]) * PARAM_PHASE_KNIGHT;
  phase += popcount(b->p[W][BISHOP] | b->p[B][BISHOP]) * PARAM_PHASE_BISHOP;
  phase += popcount(b->p[W][R] | b->p[B][R]) * PARAM_PHASE_ROOK;
  phase += popcount(b->p[W][Q] | b->p[B][Q]) * PARAM_PHASE_QUEEN;
  if (phase > PARAM_PHASE_MAX) phase = PARAM_PHASE_MAX;
  if (phase < 0) phase = 0;
  return phase;
}

static int eval_hanging_pieces(const Board *b, int c) {
  int penalty = 0;
  for (int pc = P; pc <= Q; pc++) {
    U64 bb = b->p[c][pc];
    while (bb) {
      int sq = POP(bb);
      bb &= bb - 1;
      if (!is_attacked(b, sq, c)) continue;
      if (is_attacked(b, sq, c ^ 1)) continue;
      penalty += (piece_val[pc] * PARAM_HANGING_PENALTY_PCT) / 100;
    }
  }
  return c == W ? -penalty : penalty;
}

static int eval_material_pst(const Board *b) {
  int score = 0;
  for (int c = 0; c < 2; c++) {
    int sign = (c == W) ? 1 : -1;
    for (int p = 0; p < 6; p++) {
      U64 bb = b->p[c][p];
      while (bb) {
        int sq = POP(bb);
        bb &= bb - 1;
        score += sign * (piece_val[p] + pst[c][p][sq]);
      }
    }
  }
  return score;
}

static int eval_tempo(const Board *b) {
  return b->side == W ? PARAM_TEMPO_BONUS : -PARAM_TEMPO_BONUS;
}

int eval(const Board *b) {
  int base = eval_material_pst(b);
  int pawn = eval_pawn_structure_side(b, W) + eval_pawn_structure_side(b, B);
  int king_safe = eval_king_safety_side(b, W) + eval_king_safety_side(b, B);
  int bishop_pair = eval_bishop_pair_side(b, W) + eval_bishop_pair_side(b, B);
  int rook_act = eval_rook_activity_side(b, W) + eval_rook_activity_side(b, B);
  int hanging = eval_hanging_pieces(b, W) + eval_hanging_pieces(b, B);
  int king_attack = eval_king_attack(b);
  int mobility = eval_mobility(b);
  int center = eval_center_control(b);
  int tempo = eval_tempo(b);

  int phase = eval_phase(b);
  int mg_w = phase;
  int eg_w = PARAM_PHASE_MAX - phase;

  int mg = king_safe * PARAM_KING_SAFETY_WEIGHT + king_attack + mobility * PARAM_MOBILITY_WEIGHT + tempo + center;
  int eg = pawn * PARAM_PAWN_STRUCTURE_WEIGHT + rook_act + bishop_pair + hanging + tempo + center / 2;

  int score = base + (mg * mg_w + eg * eg_w) / PARAM_PHASE_MAX;
  if (b->side == B) score = -score;
  return score;
}
