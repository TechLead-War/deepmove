#ifndef UCI_H
#define UCI_H

#include "types.h"

const char *move_to_uci(Move m);
int uci_to_move(const Board *b, const char *uci, Move *out);
const char *uci_last_error(void);

#endif
