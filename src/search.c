#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "tables.h"
#include "types.h"
#include <stddef.h>

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
    if (pv_best && he->best && move_is_legal(b, he->best)) *pv_best = he->best;
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
  board_clear_hist();
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
