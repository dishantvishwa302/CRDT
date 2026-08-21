#ifndef SYNCTEXT_CRDT_H
#define SYNCTEXT_CRDT_H

#include "types.h"
#include <vector>

// Last-Write-Wins register per line.
// Apply updates in any order: the later timestamp (then smaller user_id) wins.
bool update_wins(const Update &u, const LineMeta &current);
void apply_update(Editor &e, const Update &u);
bool apply_if_wins(Editor &e, const Update &u);
int apply_all(Editor &e, const std::vector<Update> &updates);

#endif
