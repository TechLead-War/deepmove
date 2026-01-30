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

int main(int argc, char **argv) {
  Board b;
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IOLBF, 0);
  engine_init();

  if (argc > 1 && is_interactive_arg(argv[1])) {
    int us = our_color_from_arg(argv[1]);
    board_reset(&b);
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
          printf("(none) %lldms d=%d kn=%lld nps=%lld\n", ms, depth_done, kn, nps);
          fflush(stdout);
          break;
        }
        printf("%s %lldms d=%d kn=%lld nps=%lld\n", move_to_uci(best), ms, depth_done, kn, nps);
        fflush(stdout);
        if (!make_move(&b, best)) break;
        board_sync(&b);
      } else {
        board_sync(&b);
        char buf[32];
        if (!fgets(buf, sizeof buf, stdin)) break;
        trim_newline(buf);
        if (!buf[0] || str_eq_ignore_case(buf, "quit")) break;
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
  if (best) {
    printf("%s %lldms d=%d kn=%lld nps=%lld\n", move_to_uci(best), ms, depth_done, kn, nps);
  } else {
    printf("(none) %lldms d=%d kn=%lld nps=%lld\n", ms, depth_done, kn, nps);
  }
  fflush(stdout);
  return 0;
}
