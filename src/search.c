#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "tables.h"
#include "types.h"

#include <string.h>

#define QMAX 32
#define NULL_DEPTH 2
#define LMR_DEPTH 4
#define LMR_MOVES 4

static int move_score_capture(const Board *b, Move m) {
  int to = TO(m), from = FROM(m), fl = FLAGS(m);
  int cap = b->piece_on[to];
  int victim = (cap >= 0) ? (cap % 6) : (fl == M_EP ? P : -1);
  if (victim < 0) return 0;
  int piece = b->piece_on[from];
  if (piece < 0) return 0;
  int pc = piece % 6;
  return 10000 + piece_val[victim] * 10 - piece_val[pc];
}

static void sort_captures(Board *b, MoveList *ml) {
  int i, j;
  for (i = 0; i < ml->n - 1; i++) {
    int best = i;
    int best_sc = move_score_capture(b, ml->m[i]);
    for (j = i + 1; j < ml->n; j++) {
      int sc = move_score_capture(b, ml->m[j]);
      if (sc > best_sc) { best = j; best_sc = sc; }
    }
    if (best != i) {
      Move t = ml->m[i]; ml->m[i] = ml->m[best]; ml->m[best] = t;
    }
  }
}

static int quiesce(Board *b, int alpha, int beta, int qply) {
  int stand = eval(b);
  if (stand >= beta) return beta;
  if (stand > alpha) alpha = stand;
  if (qply >= QMAX) return stand;
  MoveList ml;
  gen_captures(b, &ml);
  sort_captures(b, &ml);
  int best = stand;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (!move_is_legal(b, m)) continue;
    make_move(b, m);
    int score = -quiesce(b, -beta, -alpha, qply + 1);
    unmake_move(b, m);
    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
    if (score > best) best = score;
  }
  return best;
}

static void score_moves(const Board *b, MoveList *ml, Move hash_move, int *scores) {
  for (int i = 0; i < ml->n; i++) {
    Move m = ml->m[i];
    if (m == hash_move) { scores[i] = 20000; continue; }
    int sc = move_score_capture(b, m);
    scores[i] = sc > 0 ? sc : 0;
  }
}

static void order_moves(MoveList *ml, int *scores) {
  int i, j;
  for (i = 0; i < ml->n - 1; i++) {
    int best = i;
    for (j = i + 1; j < ml->n; j++)
      if (scores[j] > scores[best]) best = j;
    if (best != i) {
      Move t = ml->m[i]; ml->m[i] = ml->m[best]; ml->m[best] = t;
      int st = scores[i]; scores[i] = scores[best]; scores[best] = st;
    }
  }
}

static int search_inner(Board *b, int depth, int alpha, int beta, Move *pv_best) {
  if (depth <= 0) return quiesce(b, alpha, beta, 0);
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
  if (depth >= 3 && !is_attacked(b, b->king_sq[b->side], b->side)) {
    b->side ^= 1;
    int null_score = -search_inner(b, depth - 1 - NULL_DEPTH, -beta, -beta + 1, NULL);
    b->side ^= 1;
    if (null_score >= beta) return beta;
  }
  int best = -INF;
  Move best_m = 0;
  Move hash_move = (he->key == key && he->best) ? he->best : 0;
  int scores[MAX_MOVES];
  score_moves(b, &ml, hash_move, scores);
  order_moves(&ml, scores);
  if (hash_move)
    for (int i = 0; i < ml.n; i++)
      if (ml.m[i] == hash_move) { ml.m[i] = ml.m[0]; ml.m[0] = hash_move; scores[i] = scores[0]; scores[0] = 20000; break; }
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (!move_is_legal(b, m)) continue;
    int is_cap = (b->piece_on[TO(m)] >= 0) || (FLAGS(m) == M_EP);
    if (depth >= LMR_DEPTH && !is_cap && m != hash_move && i >= LMR_MOVES) {
      make_move(b, m);
      int rscore = -search_inner(b, depth - 2, -beta, -alpha, NULL);
      unmake_move(b, m);
      if (rscore >= beta) return beta;
    }
    make_move(b, m);
    int score;
    if (depth <= 1) score = -quiesce(b, -beta, -alpha, 0);
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
  memset(tt, 0, sizeof(tt));
  Move best = 0;
  int alpha = -INF, beta = INF;
  int d, s = 0;
  for (d = 1; d <= depth; d++) {
    Move pv_move = 0;
    int window_alpha = alpha, window_beta = beta;
    if (d >= 3 && alpha > -MATE + 500 && beta < MATE - 500) {
      int delta = 60;
      window_alpha = s - delta;
      window_beta = s + delta;
      if (window_alpha < alpha) window_alpha = alpha;
      if (window_beta > beta) window_beta = beta;
    }
    for (;;) {
      pv_move = 0;
      s = search_inner(b, d, window_alpha, window_beta, &pv_move);
      if (pv_move) best = pv_move;
      if (score) *score = s;
      if (s <= window_alpha && window_alpha > -MATE + 500) {
        window_beta = window_alpha;
        window_alpha = -INF;
      } else if (s >= window_beta && window_beta < MATE - 500) {
        window_alpha = window_beta;
        window_beta = INF;
      } else break;
    }
    if (d >= 3 && alpha > -MATE + 500 && beta < MATE - 500) {
      if (s > alpha) alpha = s - 30;
      if (s < beta) beta = s + 30;
    }
    if (s >= MATE - 64 || s <= -MATE + 64) break;
  }
  return best;
}
