#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"

Move search(Board *b, int depth, int *score);
int search_last_completed_depth(void);
long long search_last_nodes(void);

#endif
