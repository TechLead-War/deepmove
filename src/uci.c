#include "uci.h"
#include "movegen.h"
#include "types.h"
#include <stdio.h>

const char *move_to_uci(Move m) {
  static char buf[16];
  int f = FROM(m), t = TO(m);
  sprintf(buf, "%c%c -> %c%c", 'a' + FILE(f), '1' + RANK(f), 'a' + FILE(t), '1' + RANK(t));
  if (FLAGS(m) == M_PROMO) {
    int pr = PROMO_PC(m);
    buf[8] = (pr == 0) ? 'n' : (pr == 1) ? 'b' : (pr == 2) ? 'r' : 'q';
    buf[9] = 0;
  }
  return buf;
}

int uci_to_move(const Board *b, const char *uci, Move *out) {
  if (!uci || uci[0] < 'a' || uci[0] > 'h' || uci[1] < '1' || uci[1] > '8' || uci[2] < 'a' || uci[2] > 'h' || uci[3] < '1' || uci[3] > '8') return 0;
  int from = (uci[1] - '1') * 8 + (uci[0] - 'a');
  int to = (uci[3] - '1') * 8 + (uci[2] - 'a');
  MoveList ml;
  gen_moves(b, &ml);
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (FROM(m) == from && TO(m) == to) {
      if (FLAGS(m) == M_PROMO) {
        char pr = uci[4] | 32;
        int prn = (pr == 'n') ? 0 : (pr == 'b') ? 1 : (pr == 'r') ? 2 : 3;
        if (PROMO_PC(m) == prn) { *out = m; return 1; }
      } else { *out = m; return 1; }
    }
  }
  return 0;
}
