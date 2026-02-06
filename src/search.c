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
#include <limits.h>

static Move killer_moves[2][MAX_DEPTH];
static int history_heur[2][64][64];
static int search_abort;
static long long search_nodes;
static int search_time_ms;
static clock_t search_deadline;
static int search_last_depth;
static int search_exclude_active;
static Move search_exclude_move;
static U64 search_exclude_key;
static int search_exclude_ply;

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
  if (score > MATE - PARAM_MATE_SCORE_WINDOW) return score + ply;
  if (score < -MATE + PARAM_MATE_SCORE_WINDOW) return score - ply;
  return score;
}

static inline int score_from_tt(int score, int ply) {
  if (score > MATE - PARAM_MATE_SCORE_WINDOW) return score - ply;
  if (score < -MATE + PARAM_MATE_SCORE_WINDOW) return score + ply;
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
  if (search_nodes % PARAM_TIME_CHECK_INTERVAL == 0) {
    if (clock() >= search_deadline) search_abort = 1;
  }
}

static inline int king_sq_safe(const Board *b, int side) {
  int ksq = b->king_sq[side];
  if (ksq < 0 || ksq > 63) {
    U64 kbb = b->p[side][K];
    ksq = kbb ? POP(kbb) : -1;
  }
  return ksq;
}

static inline int in_check_now(const Board *b) {
  int ksq = king_sq_safe(b, b->side);
  return (ksq >= 0) ? is_attacked(b, ksq, b->side) : 0;
}

static inline int has_non_pawn_material(const Board *b, int side) {
  return (b->p[side][N] | b->p[side][BISHOP] | b->p[side][R] | b->p[side][Q]) != 0;
}

static inline int should_try_null(const Board *b, int depth, int in_check) {
  return depth >= 3 && !in_check && has_non_pawn_material(b, b->side);
}

static inline int should_lmr(int depth, int is_cap, Move m, Move hash_move, int move_index) {
  return depth >= PARAM_LMR_DEPTH && !is_cap && m != hash_move && move_index >= PARAM_LMR_MOVES;
}

static inline int should_lmp(int depth, int in_check, int is_cap, int move_index) {
  return !in_check && depth <= PARAM_LMP_DEPTH && !is_cap && move_index >= PARAM_LMP_MOVES;
}

static inline int is_root_excluded(const Board *b, Move m) {
  return search_exclude_active &&
         b->ply == search_exclude_ply &&
         b->key == search_exclude_key &&
         m == search_exclude_move;
}

static inline void tt_store(HashEntry *he, U64 key, int depth, int alpha_orig, int beta, int score, Move best, int ply) {
  he->key = key;
  he->depth = depth;
  he->score = score_to_tt(score, ply);
  he->flag = (score >= beta) ? 1 : (score <= alpha_orig) ? 2 : 0;
  he->best = best;
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
  if (*h > PARAM_HISTORY_MAX) *h /= 2;
}

static int move_score_capture(const Board *b, Move m) {
  int to = TO(m), from = FROM(m), fl = FLAGS(m);
  int cap = b->piece_on[to];
  int victim = (cap >= 0) ? (cap % 6) : (fl == M_EP ? P : -1);
  int piece = b->piece_on[from];
  int score = 0;
  if (victim >= 0 && piece >= 0) {
    int pc = piece % 6;
    score = PARAM_CAPTURE_BASE_SCORE + piece_val[victim] * PARAM_CAPTURE_MVV_LVA_FACTOR - piece_val[pc];
  }
  if (fl == M_PROMO) score += PARAM_PROMO_CAPTURE_BONUS + piece_val[promo_piece(m)];
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
  int in_check = in_check_now(b);
  MoveList ml;
  gen_moves(b, &ml);
  if (!in_check) {
    int j = 0;
    for (int i = 0; i < ml.n; i++) {
      Move m = ml.m[i];
      if (!is_capture(b, m) && FLAGS(m) != M_PROMO) continue;
      if (see(b, m) < 0) continue;
      ml.m[j++] = m;
    }
    ml.n = j;
  }
  sort_captures(b, &ml);
  int best = stand;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (is_root_excluded(b, m)) continue;
    if (!move_is_legal(b, m)) continue;
    if (!make_move(b, m)) continue;
    int score = -quiesce(b, -beta, -alpha, qply + 1);
    unmake_move(b, m);
    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
    if (score > best) best = score;
  }
  return best;
}

static int move_gives_check(Board *b, Move m) {
  if (!make_move(b, m)) return 0;
  int check = in_check_now(b);
  unmake_move(b, m);
  return check;
}

static void score_moves(Board *b, MoveList *ml, Move hash_move, int *scores) {
  int ply = clamp_ply(b->ply);
  for (int i = 0; i < ml->n; i++) {
    Move m = ml->m[i];
    int score = 0;
    if (m == hash_move) {
      score = PARAM_HASH_MOVE_SCORE;
    } else if (FLAGS(m) == M_PROMO) {
      int promo = promo_piece(m);
      score = PARAM_PROMO_BASE_SCORE + piece_val[promo];
      if (is_capture(b, m)) score += move_score_capture(b, m);
    } else if (is_capture(b, m)) {
      int sc = move_score_capture(b, m);
      int see_score = see(b, m);
      if (see_score < 0) sc -= PARAM_SEE_BAD_PENALTY;
      else if (see_score > 0) sc += PARAM_SEE_GOOD_BONUS;
      score = sc > 0 ? sc : 0;
    } else if (m == killer_moves[0][ply]) {
      score = PARAM_KILLER_SCORE_1;
    } else if (m == killer_moves[1][ply]) {
      score = PARAM_KILLER_SCORE_2;
    } else {
      score = history_heur[b->side][FROM(m)][TO(m)];
    }
    if (PARAM_CHECK_BONUS > 0 && move_gives_check(b, m)) score += PARAM_CHECK_BONUS;
    scores[i] = score;
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
  if (b->fifty >= PARAM_FIFTY_MOVE_LIMIT) return (b->side == W ? PARAM_CONTEMPT : -PARAM_CONTEMPT);
  if (board_is_repetition(b)) return (b->side == W ? PARAM_CONTEMPT : -PARAM_CONTEMPT);
  int alpha_orig = alpha;
  int in_check = in_check_now(b);
  if (in_check && depth < MAX_DEPTH - 1) depth++;
  if (depth <= 0) return quiesce(b, alpha, beta, 0);

  int static_eval = 0;
  if (!in_check) {
    static_eval = eval(b);
    if (depth <= 1 && static_eval + PARAM_FUTILITY_MARGIN <= alpha) return static_eval;
    if (depth <= 2 && static_eval + PARAM_RAZOR_MARGIN <= alpha) return quiesce(b, alpha, beta, 0);
  }
  U64 key = b->key;
  HashEntry *he = &tt[key & HASH_MASK];
  if (he->key == key && he->depth >= depth) {
    int tt_score = score_from_tt(he->score, b->ply);
    int best_ok = he->best && move_is_legal(b, he->best);
    if (pv_best && best_ok) *pv_best = he->best;
    if (he->flag == 0 && (!pv_best || best_ok)) return tt_score;
    if (he->flag == 1 && tt_score >= beta) return tt_score;
    if (he->flag == 2 && tt_score <= alpha) return tt_score;
  }
  MoveList ml;
  gen_moves(b, &ml);
  if (ml.n == 0) {
    if (in_check) return -MATE + b->ply;
    return (b->side == W ? PARAM_CONTEMPT : -PARAM_CONTEMPT);
  }
  if (should_try_null(b, depth, in_check)) {
    NullState ns;
    make_null(b, &ns);
    int null_score = -search_inner(b, depth - PARAM_NULL_REDUCTION - PARAM_NULL_DEPTH, -beta, -beta + 1, NULL);
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
      if (ml.m[i] == hash_move) { ml.m[i] = ml.m[0]; ml.m[0] = hash_move; scores[i] = scores[0]; scores[0] = PARAM_HASH_MOVE_TOP_SCORE; break; }
  int legal = 0;
  int first = 1;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (is_root_excluded(b, m)) continue;
    if (!move_is_legal(b, m)) continue;
    legal++;
    int is_cap = (b->piece_on[TO(m)] >= 0) || (FLAGS(m) == M_EP);
    if (should_lmp(depth, in_check, is_cap, i)) break;
    if (should_lmr(depth, is_cap, m, hash_move, i)) {
      if (!make_move(b, m)) continue;
      int gives_check = in_check_now(b);
      int rscore = alpha + 1;
      if (!gives_check) {
        int rdepth = depth - PARAM_LMR_REDUCTION;
        if (rdepth <= 0) rscore = -quiesce(b, -beta, -alpha, 0);
        else rscore = -search_inner(b, rdepth, -beta, -alpha, NULL);
      }
      unmake_move(b, m);
      if (!gives_check) {
        if (rscore <= alpha) continue;
        if (rscore >= beta) {
          note_beta_cutoff(b, m, depth);
          return beta;
        }
      }
    }
    if (!make_move(b, m)) continue;
    int gives_check = in_check_now(b);
    int next_depth = depth - 1;
    if (gives_check && depth > 1 && next_depth < MAX_DEPTH - 1) next_depth += 1;
    int score;
    if (first) {
      if (next_depth <= 0) score = -quiesce(b, -beta, -alpha, 0);
      else score = -search_inner(b, next_depth, -beta, -alpha, NULL);
      first = 0;
    } else {
      if (next_depth <= 0) score = -quiesce(b, -alpha - 1, -alpha, 0);
      else score = -search_inner(b, next_depth, -alpha - 1, -alpha, NULL);
      if (score > alpha && score < beta) {
        if (next_depth <= 0) score = -quiesce(b, -beta, -alpha, 0);
        else score = -search_inner(b, next_depth, -beta, -alpha, NULL);
      }
    }
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
  tt_store(he, key, depth, alpha_orig, beta, best, best_m, b->ply);
  return best;
}

Move search(Board *b, int depth, int *score) {
  if (PARAM_TT_CLEAR_ON_NEW_SEARCH) tt_clear();
  clear_search_heuristics();
  search_abort = 0;
  search_nodes = 0;
  search_last_depth = 0;
  if (depth < 1) depth = 1;
  if (depth > MAX_DEPTH - 1) depth = MAX_DEPTH - 1;
  search_time_ms = PARAM_DEFAULT_MOVE_TIME_MS;
  int increment_ms = PARAM_MOVE_TIME_INCREMENT_MS;
  const char *env_time = getenv("MOVE_TIME_MS");
  if (env_time && *env_time) {
    int v = atoi(env_time);
    if (v > 0) search_time_ms = v;
    else search_time_ms = 0;
  }
  const char *env_inc = getenv("MOVE_INCREMENT_MS");
  if (env_inc && *env_inc) {
    int v = atoi(env_inc);
    if (v >= 0) increment_ms = v;
  }
  if (search_time_ms > 0 && increment_ms > 0) {
    int moves_played = b->ply;
    long long total = (long long)search_time_ms + (long long)increment_ms * moves_played;
    if (total > INT_MAX) total = INT_MAX;
    search_time_ms = (int)total;
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
    if (d >= 3 && alpha > -MATE + PARAM_MATE_WINDOW_MARGIN && beta < MATE - PARAM_MATE_WINDOW_MARGIN) {
      int delta = PARAM_ASPIRATION_DELTA;
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
      if (s <= window_alpha && window_alpha > -MATE + PARAM_MATE_WINDOW_MARGIN) {
        window_beta = window_alpha;
        window_alpha = -INF;
      } else if (s >= window_beta && window_beta < MATE - PARAM_MATE_WINDOW_MARGIN) {
        window_alpha = window_beta;
        window_beta = INF;
      } else break;
    }
    if (search_abort) break;
    search_last_depth = d;
    if (d >= 3 && alpha > -MATE + PARAM_MATE_WINDOW_MARGIN && beta < MATE - PARAM_MATE_WINDOW_MARGIN) {
      if (s > alpha) alpha = s - PARAM_ASPIRATION_GROW;
      if (s < beta) beta = s + PARAM_ASPIRATION_GROW;
    }
    if (s >= MATE - PARAM_MATE_SCORE_CUTOFF || s <= -MATE + PARAM_MATE_SCORE_CUTOFF) break;
  }
  search_exclude_active = 0;
  return best;
}

int search_last_completed_depth(void) {
  return search_last_depth;
}

long long search_last_nodes(void) {
  return search_nodes;
}

void search_set_root_exclude(Move m, U64 key, int ply) {
  search_exclude_move = m;
  search_exclude_key = key;
  search_exclude_ply = ply;
  search_exclude_active = (m != 0);
}
