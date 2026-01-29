#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "params.h"
#include "tables.h"
#include "types.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>

static Move killer_moves[2][MAX_DEPTH];
static int history_heur[2][64][64];
static int search_abort;
static int search_nodes;
static int search_time_ms;
static clock_t search_deadline;

typedef struct {
  int ep;
  int ply;
  U64 key;
} NullState;

static inline int promo_piece(Move m) {
  int pr = PROMO_PC(m);
  if (pr == 0) return N;
  if (pr == 1) return BISHOP;
  if (pr == 2) return R;
  return Q;
}

static inline int is_capture(const Board *b, Move m) {
  return (b->piece_on[TO(m)] >= 0) || (FLAGS(m) == M_EP);
}

static inline int clamp_ply(int ply) {
  if (ply < 0) return 0;
  if (ply >= MAX_DEPTH) return MAX_DEPTH - 1;
  return ply;
}

static inline int score_to_tt(int score, int ply) {
  if (score > MATE - 1000) return score + ply;
  if (score < -MATE + 1000) return score - ply;
  return score;
}

static inline int score_from_tt(int score, int ply) {
  if (score > MATE - 1000) return score - ply;
  if (score < -MATE + 1000) return score + ply;
  return score;
}

static inline void make_null(Board *b, NullState *st) {
  st->ep = b->ep;
  st->ply = b->ply;
  st->key = b->key;
  b->key = tables_key_after_null(b);
  b->ep = -1;
  b->side ^= 1;
  b->ply++;
}

static inline void unmake_null(Board *b, const NullState *st) {
  b->side ^= 1;
  b->ep = st->ep;
  b->ply = st->ply;
  b->key = st->key;
}

static inline void clear_search_heuristics(void) {
  memset(killer_moves, 0, sizeof(killer_moves));
  memset(history_heur, 0, sizeof(history_heur));
}

static inline void search_check_time(void) {
  if (search_time_ms <= 0) return;
  if ((search_nodes & 1023) == 0) {
    if (clock() >= search_deadline) search_abort = 1;
  }
}

static inline void note_beta_cutoff(const Board *b, Move m, int depth) {
  if (is_capture(b, m) || FLAGS(m) == M_PROMO) return;
  int ply = clamp_ply(b->ply);
  if (killer_moves[0][ply] != m) {
    killer_moves[1][ply] = killer_moves[0][ply];
    killer_moves[0][ply] = m;
  }
  int from = FROM(m), to = TO(m);
  int *h = &history_heur[b->side][from][to];
  *h += depth * depth;
  if (*h > 2000000) *h /= 2;
}

static int move_score_capture(const Board *b, Move m) {
  int to = TO(m), from = FROM(m), fl = FLAGS(m);
  int cap = b->piece_on[to];
  int victim = (cap >= 0) ? (cap % 6) : (fl == M_EP ? P : -1);
  int piece = b->piece_on[from];
  int score = 0;
  if (victim >= 0 && piece >= 0) {
    int pc = piece % 6;
    score = 10000 + piece_val[victim] * 10 - piece_val[pc];
  }
  if (fl == M_PROMO) score += 8000 + piece_val[promo_piece(m)];
  return score;
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
  search_nodes++;
  search_check_time();
  if (search_abort) return eval(b);
  int stand = eval(b);
  if (stand >= beta) return beta;
  if (stand > alpha) alpha = stand;
  if (qply >= PARAM_QMAX) return stand;
  MoveList ml;
  gen_moves(b, &ml);
  int j = 0;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (!is_capture(b, m) && FLAGS(m) != M_PROMO) continue;
    ml.m[j++] = m;
  }
  ml.n = j;
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
  int ply = clamp_ply(b->ply);
  for (int i = 0; i < ml->n; i++) {
    Move m = ml->m[i];
    if (m == hash_move) { scores[i] = 30000; continue; }
    if (FLAGS(m) == M_PROMO) {
      int promo = promo_piece(m);
      scores[i] = 25000 + piece_val[promo];
      if (is_capture(b, m)) scores[i] += move_score_capture(b, m);
      continue;
    }
    if (is_capture(b, m)) {
      int sc = move_score_capture(b, m);
      scores[i] = sc > 0 ? sc : 0;
      continue;
    }
    if (m == killer_moves[0][ply]) { scores[i] = 9000; continue; }
    if (m == killer_moves[1][ply]) { scores[i] = 8000; continue; }
    scores[i] = history_heur[b->side][FROM(m)][TO(m)];
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
  search_nodes++;
  search_check_time();
  if (search_abort) return eval(b);
  int alpha_orig = alpha;
  int in_check = is_attacked(b, b->king_sq[b->side], b->side);
  if (in_check && depth < MAX_DEPTH - 1) depth++;
  if (depth <= 0) return quiesce(b, alpha, beta, 0);
  U64 key = b->key;
  HashEntry *he = &tt[key & HASH_MASK];
  if (he->key == key && he->depth >= depth) {
    int tt_score = score_from_tt(he->score, b->ply);
    if (pv_best && he->best && move_is_legal(b, he->best)) *pv_best = he->best;
    if (he->flag == 0) return tt_score;
    if (he->flag == 1 && tt_score >= beta) return tt_score;
    if (he->flag == 2 && tt_score <= alpha) return tt_score;
  }
  MoveList ml;
  gen_moves(b, &ml);
  if (ml.n == 0) {
    if (in_check) return -MATE + b->ply;
    return 0;
  }
  if (depth >= 3 && !in_check) {
    NullState ns;
    make_null(b, &ns);
    int null_score = -search_inner(b, depth - 1 - PARAM_NULL_DEPTH, -beta, -beta + 1, NULL);
    unmake_null(b, &ns);
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
  int legal = 0;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (!move_is_legal(b, m)) continue;
    legal++;
    int is_cap = (b->piece_on[TO(m)] >= 0) || (FLAGS(m) == M_EP);
    if (depth >= PARAM_LMR_DEPTH && !is_cap && m != hash_move && i >= PARAM_LMR_MOVES) {
      make_move(b, m);
      int rscore = -search_inner(b, depth - 2, -beta, -alpha, NULL);
      unmake_move(b, m);
      if (rscore <= alpha) continue;
      if (rscore >= beta) {
        note_beta_cutoff(b, m, depth);
        return beta;
      }
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
      if (best >= beta) {
        note_beta_cutoff(b, m, depth);
        break;
      }
      if (best > alpha) alpha = best;
    }
  }
  if (legal == 0) {
    if (in_check) return -MATE + b->ply;
    return 0;
  }
  if (search_abort) return eval(b);
  he->key = key;
  he->depth = depth;
  he->score = score_to_tt(best, b->ply);
  he->flag = (best >= beta) ? 1 : (best <= alpha_orig) ? 2 : 0;
  he->best = best_m;
  return best;
}

Move search(Board *b, int depth, int *score) {
  board_clear_hist();
  memset(tt, 0, sizeof(tt));
  clear_search_heuristics();
  search_abort = 0;
  search_nodes = 0;
  search_time_ms = PARAM_DEFAULT_MOVE_TIME_MS;
  const char *env_time = getenv("MOVE_TIME_MS");
  if (env_time && *env_time) {
    int v = atoi(env_time);
    if (v > 0) search_time_ms = v;
    else search_time_ms = 0;
  }
  if (search_time_ms > 0) {
    search_deadline = clock() + (clock_t)((search_time_ms * CLOCKS_PER_SEC) / 1000);
  }
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
      if (search_abort) break;
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
    if (search_abort) break;
    if (d >= 3 && alpha > -MATE + 500 && beta < MATE - 500) {
      if (s > alpha) alpha = s - 30;
      if (s < beta) beta = s + 30;
    }
    if (s >= MATE - 64 || s <= -MATE + 64) break;
  }
  return best;
}
