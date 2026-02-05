#include "engine.h"
#include "board.h"
#include "movegen.h"
#include "params.h"
#include "search.h"
#include "uci.h"
#include "types.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char tt_cache_path[PATH_MAX];
static int tt_save_enabled = 0;

static void save_tt_on_exit(void) {
  if (tt_save_enabled && tt_cache_path[0]) {
    tt_save(tt_cache_path);
  }
}

static void init_tt_cache(void) {
  int do_load = PARAM_TT_LOAD_ON_START;
  int do_save = PARAM_TT_SAVE_ON_EXIT;
  const char *path = PARAM_TT_CACHE_PATH;
  const char *env_path = getenv("TT_CACHE_PATH");
  if (env_path && *env_path) path = env_path;
  const char *env_load = getenv("TT_LOAD");
  if (env_load && *env_load) do_load = atoi(env_load) != 0;
  const char *env_save = getenv("TT_SAVE");
  if (env_save && *env_save) do_save = atoi(env_save) != 0;
  const char *env_clear = getenv("TT_CLEAR");
  if (env_clear && *env_clear && atoi(env_clear) != 0) {
    do_load = 0;
    tt_clear();
  }
  if (do_load) tt_load(path);
  if (do_save && path && *path) {
    strncpy(tt_cache_path, path, sizeof(tt_cache_path) - 1);
    tt_cache_path[sizeof(tt_cache_path) - 1] = '\0';
    tt_save_enabled = 1;
    atexit(save_tt_on_exit);
  }
}

static int str_eq_ignore_case(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
  }
  return *a == *b;
}

static int is_interactive_arg(const char *s) {
  if (!s || !*s) return 0;
  char c = (char)tolower((unsigned char)*s);
  if (s[1] == '\0') return (c == 'w' || c == 'b');
  if (c == 'w' && str_eq_ignore_case(s, "white")) return 1;
  if (c == 'b' && str_eq_ignore_case(s, "black")) return 1;
  return 0;
}

static int our_color_from_arg(const char *s) {
  if (!s || !*s) return W;
  if (tolower((unsigned char)*s) == 'b') return B;
  return W;
}

static int is_fen(const char *s) {
  if (!s) return 0;
  for (; *s; s++) if (*s == '/') return 1;
  return 0;
}

static void trim_newline(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) { n--; s[n] = '\0'; }
}

static int starts_with_cmd(const char *s, const char *cmd) {
  size_t n = strlen(cmd);
  return strncmp(s, cmd, n) == 0;
}

static Move move_stack[HIST_SIZE];
static int side_stack[HIST_SIZE];
static int move_top = 0;

static void reset_move_stack(void) {
  move_top = 0;
}

static void record_move(Move m, int side) {
  if (move_top >= HIST_SIZE) return;
  move_stack[move_top] = m;
  side_stack[move_top] = side;
  move_top++;
}

static int pop_move(Move *m, int *side) {
  if (move_top <= 0) return 0;
  move_top--;
  if (m) *m = move_stack[move_top];
  if (side) *side = side_stack[move_top];
  return 1;
}

static int peek_last_side(void) {
  if (move_top <= 0) return -1;
  return side_stack[move_top - 1];
}

static Move last_move_by_side(int side) {
  for (int i = move_top - 1; i >= 0; i--) {
    if (side_stack[i] == side) return move_stack[i];
  }
  return 0;
}

int main(int argc, char **argv) {
  Board b;
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IOLBF, 0);
  engine_init();
  init_tt_cache();

  if (argc > 1 && is_interactive_arg(argv[1])) {
    int us = our_color_from_arg(argv[1]);
    board_reset(&b);
    reset_move_stack();
    Move last_engine_move = 0;
    for (;;) {
      if (b.side == us) {
        board_sync(&b);
        MoveList ml;
        gen_moves(&b, &ml);
        if (ml.n == 0) break;
        fprintf(stderr, "Thinking... ");
        fflush(stderr);
        int score;
        Board b_search = b;
        clock_t start = clock();
        Move best = search(&b_search, PARAM_DEFAULT_SEARCH_DEPTH, &score);
        int depth_done = search_last_completed_depth();
        long long nodes = search_last_nodes();
        clock_t end = clock();
        long long ms = (long long)(end - start) * 1000 / CLOCKS_PER_SEC;
        long long kn = nodes / 1000;
        long long nps = 0;
        if (ms > 0) nps = (nodes * 1000) / ms;
        if (!best || !move_is_legal(&b, best)) {
          MoveList ml_fallback;
          gen_moves(&b, &ml_fallback);
          for (int i = 0; i < ml_fallback.n; i++) {
            if (move_is_legal(&b, ml_fallback.m[i])) { best = ml_fallback.m[i]; break; }
          }
        }
        if (!best || !move_is_legal(&b, best)) {
          printf("(none) %lldms d=%d kn=%lld nps=%lld\n", ms, depth_done, kn, nps);
          fflush(stdout);
          break;
        }
        printf("%s %lldms d=%d kn=%lld nps=%lld\n", move_to_uci(best), ms, depth_done, kn, nps);
        fflush(stdout);
        if (!make_move(&b, best)) break;
        record_move(best, us);
        last_engine_move = best;
        board_sync(&b);
      } else {
        board_sync(&b);
        char buf[128];
        if (!fgets(buf, sizeof buf, stdin)) break;
        trim_newline(buf);
        if (!buf[0] || str_eq_ignore_case(buf, "quit")) break;
        if (str_eq_ignore_case(buf, "undo")) {
          if (peek_last_side() == us) {
            Move m;
            pop_move(&m, NULL);
            unmake_move(&b, m);
            last_engine_move = last_move_by_side(us);
          } else {
            fprintf(stderr, "no engine move to undo\n");
          }
          continue;
        }
        if (str_eq_ignore_case(buf, "takeback") || str_eq_ignore_case(buf, "undoopp")) {
          if (move_top >= 2 && side_stack[move_top - 1] == us && side_stack[move_top - 2] == (us ^ 1)) {
            Move m;
            pop_move(&m, NULL);
            unmake_move(&b, m);
            pop_move(&m, NULL);
            unmake_move(&b, m);
            last_engine_move = last_move_by_side(us);
          } else {
            fprintf(stderr, "no opponent move to undo\n");
          }
          continue;
        }
        if (starts_with_cmd(buf, "force ") || starts_with_cmd(buf, "play ")) {
          const char *arg = buf + 6;
          if (peek_last_side() != us || !last_engine_move) {
            fprintf(stderr, "no engine move to replace\n");
            continue;
          }
          {
            Move m;
            pop_move(&m, NULL);
            unmake_move(&b, m);
          }
          search_set_root_exclude(last_engine_move, b.key, b.ply);
          tt_clear();
          Move forced;
          if (!uci_to_move(&b, arg, &forced)) {
            fprintf(stderr, "invalid forced move: %s\n", uci_last_error());
            make_move(&b, last_engine_move);
            record_move(last_engine_move, us);
            continue;
          }
          if (!make_move(&b, forced)) {
            fprintf(stderr, "forced move could not be applied\n");
            make_move(&b, last_engine_move);
            record_move(last_engine_move, us);
            continue;
          }
          record_move(forced, us);
          last_engine_move = forced;
          printf("%s 0ms d=0 kn=0 nps=0\n", move_to_uci(forced));
          fflush(stdout);
          continue;
        }
        Move m;
        if (!uci_to_move(&b, buf, &m)) {
          fprintf(stderr, "invalid move: %s\n", uci_last_error());
          continue;
        }
        if (!make_move(&b, m)) {
          board_sync(&b);
          if (!make_move(&b, m)) {
            fprintf(stderr, "invalid move: could not apply move\n");
            continue;
          }
        }
        record_move(m, us ^ 1);
      }
    }
    return 0;
  }

  if (argc > 1 && is_fen(argv[1])) {
    board_from_fen(&b, argv[1]);
  } else {
    board_reset(&b);
  }
  int score;
  clock_t start = clock();
  Move best = search(&b, PARAM_DEFAULT_SEARCH_DEPTH, &score);
  int depth_done = search_last_completed_depth();
  long long nodes = search_last_nodes();
  clock_t end = clock();
  long long ms = (long long)(end - start) * 1000 / CLOCKS_PER_SEC;
  long long kn = nodes / 1000;
  long long nps = 0;
  if (ms > 0) nps = (nodes * 1000) / ms;
  if (!best || !move_is_legal(&b, best)) {
    MoveList ml_fallback;
    gen_moves(&b, &ml_fallback);
    for (int i = 0; i < ml_fallback.n; i++) {
      if (move_is_legal(&b, ml_fallback.m[i])) { best = ml_fallback.m[i]; break; }
    }
  }
  if (best) {
    printf("%s %lldms d=%d kn=%lld nps=%lld\n", move_to_uci(best), ms, depth_done, kn, nps);
  } else {
    printf("(none) %lldms d=%d kn=%lld nps=%lld\n", ms, depth_done, kn, nps);
  }
  fflush(stdout);
  return 0;
}
