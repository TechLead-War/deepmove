#include "eval.h"
#include "movegen.h"
#include "tables.h"
#include "types.h"

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
