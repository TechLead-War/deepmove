#include "uci.h"
#include "board.h"
#include "movegen.h"
#include "types.h"

#include <stdio.h>

static char uci_err[128];

const char *uci_last_error(void) {
  return uci_err[0] ? uci_err : "unknown error";
}

const char *move_to_uci(Move m) {
  static char buf[16];
  int f = FROM(m), t = TO(m);
  sprintf(buf, "%c%c%c%c", 'a' + FILE(f), '1' + RANK(f), 'a' + FILE(t), '1' + RANK(t));
  if (FLAGS(m) == M_PROMO) {
    int pr = PROMO_PC(m);
    buf[4] = (pr == 0) ? 'n' : (pr == 1) ? 'b' : (pr == 2) ? 'r' : 'q';
    buf[5] = '\0';
  } else {
    buf[4] = '\0';
  }
  return buf;
}

static int parse_squares(const char *uci, int *from, int *to, char *promo) {
  const char *p = uci;
  int sq[2];
  int n = 0;
  *promo = 0;
  while (*p && n < 3) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    int fc = *p | 32;
    if (fc >= 'a' && fc <= 'h' && p[1] >= '1' && p[1] <= '8') {
      if (n < 2) sq[n++] = (p[1] - '1') * 8 + (fc - 'a');
      p += 2;
      continue;
    }
    if (n == 2 && (*p == 'n' || *p == 'b' || *p == 'r' || *p == 'q' || *p == 'N' || *p == 'B' || *p == 'R' || *p == 'Q')) {
      *promo = (char)(*p | 32);
      p++;
      continue;
    }
    p++;
  }
  if (n < 2) return 0;
  *from = sq[0];
  *to = sq[1];
  return 1;
}

int uci_to_move(const Board *b, const char *uci, Move *out) {
  int from, to;
  char promo;
  uci_err[0] = '\0';
  if (!uci) {
    snprintf(uci_err, sizeof(uci_err), "no move given");
    return 0;
  }
  while (*uci == ' ' || *uci == '\t') uci++;
  if (!parse_squares(uci, &from, &to, &promo)) {
    snprintf(uci_err, sizeof(uci_err), "could not parse move (use format: e2e4 or d8c7)");
    return 0;
  }
  board_sync((Board *)b);
  {
    int stm = b->side;
    int at_from = b->piece_on[from];
    if (at_from < 0 || (at_from / 6) != stm) {
      at_from = -1;
      for (int pc = 0; pc < 6; pc++)
        if ((b->p[stm][pc] >> from) & 1) {
          ((Board *)b)->piece_on[from] = stm * 6 + pc;
          break;
        }
    }
    if (b->piece_on[from] < 0 || (b->piece_on[from] / 6) != stm) {
      snprintf(uci_err, sizeof(uci_err), "no piece on source square %c%c", 'a' + (from & 7), '1' + (from >> 3));
      return 0;
    }
  }
  if (!promo) {
    Move candidate = MOVE(from, to, M_NORMAL);
    int legal = move_is_legal((Board *)b, candidate);
    if (legal) { *out = candidate; return 1; }
  }
  MoveList ml;
  gen_moves(b, &ml);
  int first = -1, count = 0;
  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (FROM(m) != from || TO(m) != to) continue;
    if (FLAGS(m) == M_PROMO) {
      int prn = (promo == 'n') ? 0 : (promo == 'b') ? 1 : (promo == 'r') ? 2 : (promo == 'q') ? 3 : -1;
      if (prn < 0 || PROMO_PC(m) != prn) continue;
    } else if (promo) continue;
    if (first < 0) first = i;
    count++;
  }
  if (first < 0) {
    board_sync((Board *)b);
    gen_moves(b, &ml);
    first = -1;
    count = 0;
    for (int i = 0; i < ml.n; i++) {
      Move m = ml.m[i];
      if (FROM(m) != from || TO(m) != to) continue;
      if (FLAGS(m) == M_PROMO) {
        int prn = (promo == 'n') ? 0 : (promo == 'b') ? 1 : (promo == 'r') ? 2 : (promo == 'q') ? 3 : -1;
        if (prn < 0 || PROMO_PC(m) != prn) continue;
      } else if (promo) continue;
      if (first < 0) first = i;
      count++;
    }
    if (first >= 0) {
      if (count == 1) { *out = ml.m[first]; return 1; }
      for (int i = 0; i < ml.n; i++) {
        Move m = ml.m[i];
        if (FROM(m) != from || TO(m) != to) continue;
        if (FLAGS(m) == M_PROMO) {
          int prn = (promo == 'n') ? 0 : (promo == 'b') ? 1 : (promo == 'r') ? 2 : (promo == 'q') ? 3 : -1;
          if (prn < 0 || PROMO_PC(m) != prn) continue;
        } else if (promo) continue;
        if (move_is_legal((Board *)b, m)) { *out = m; return 1; }
      }
      Move candidate = MOVE(from, to, M_NORMAL);
      if (move_is_legal((Board *)b, candidate)) { *out = candidate; return 1; }
      snprintf(uci_err, sizeof(uci_err), "move not legal (illegal or would leave king in check)");
      return 0;
    }
    Move candidate = MOVE(from, to, M_NORMAL);
    if (move_is_legal((Board *)b, candidate)) { *out = candidate; return 1; }
    snprintf(uci_err, sizeof(uci_err), "move not legal (illegal or would leave king in check)");
    return 0;
  }

  if (count == 1) { *out = ml.m[first]; return 1; }

  for (int i = 0; i < ml.n; i++) {
    Move m = ml.m[i];
    if (FROM(m) != from || TO(m) != to) continue;
    if (FLAGS(m) == M_PROMO) {
      int prn = (promo == 'n') ? 0 : (promo == 'b') ? 1 : (promo == 'r') ? 2 : (promo == 'q') ? 3 : -1;
      if (prn < 0 || PROMO_PC(m) != prn) continue;
    } else if (promo) continue;
    if (move_is_legal((Board *)b, m)) { *out = m; return 1; }
  }
  {
    Move candidate = MOVE(from, to, M_NORMAL);
    if (move_is_legal((Board *)b, candidate)) { *out = candidate; return 1; }
    snprintf(uci_err, sizeof(uci_err), "move not legal (illegal or would leave king in check)");
    return 0;
  }
  board_sync((Board *)b);
  if (!promo) {
    Move candidate = MOVE(from, to, M_NORMAL);
    if (move_is_legal((Board *)b, candidate)) { *out = candidate; return 1; }
  }
  snprintf(uci_err, sizeof(uci_err), "move not legal (illegal or would leave king in check)");
  return 0;
}
