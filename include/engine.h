#ifndef ENGINE_H
#define ENGINE_H

#include "board.h"
#include "search.h"
#include "tables.h"

static inline void engine_init(void) {
  init_tables();
}

#endif
