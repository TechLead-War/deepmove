#include "engine.h"
#include "board.h"
#include "movegen.h"
#include "search.h"
#include "uci.h"
#include "types.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int str_eq_ignore_case(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
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
  if (n > 0 && s[n - 1] == '\n') s[n - 1] = '\0';
}

int main(int argc, char **argv) {
  Board b;
  engine_init();

  if (argc > 1 && is_interactive_arg(argv[1])) {
    int us = our_color_from_arg(argv[1]);
    board_reset(&b);
    for (;;) {
      if (b.side == us) {
        MoveList ml;
        gen_moves(&b, &ml);
        if (ml.n == 0) break;
        int score;
        Move best = search(&b, 5, &score);
        if (!best) break;
        printf("%s\n", move_to_uci(best));
        fflush(stdout);
        make_move(&b, best);
      } else {
        char buf[32];
        if (!fgets(buf, sizeof buf, stdin)) break;
        trim_newline(buf);
        if (!buf[0] || str_eq_ignore_case(buf, "quit")) break;
        Move m;
        if (!uci_to_move(&b, buf, &m)) {
          fprintf(stderr, "invalid move\n");
          continue;
        }
        make_move(&b, m);
      }
    }
    return 0;
  }

  if (argc > 1 && is_fen(argv[1]))
    board_from_fen(&b, argv[1]);
  else
    board_reset(&b);
  int score;
  Move best = search(&b, 5, &score);
  if (best)
    printf("%s\n", move_to_uci(best));
  else
    printf("(none)\n");
  return 0;
}
