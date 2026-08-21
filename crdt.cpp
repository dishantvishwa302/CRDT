// CRDT: each line is a Last-Write-Wins register.
// Compare (timestamp, user_id). Later timestamp wins; on a tie, smaller user_id wins.
// That rule is deterministic, so every peer converges to the same document.

#include "crdt.h"

using namespace std;

void apply_update(Editor &e, const Update &u) {
    if (u.line < 0) return;
    if (u.line >= (int)e.lines.size()) {
        e.lines.resize(u.line + 1);
        e.meta.resize(u.line + 1);
    }
    e.lines[u.line] = (u.op == "delete") ? "" : u.new_text;
    e.meta[u.line] = {u.timestamp, u.user_id};
}

bool update_wins(const Update &u, const LineMeta &current) {
    if (current.timestamp == 0 && current.author.empty()) return true;
    if (u.timestamp != current.timestamp) return u.timestamp > current.timestamp;
    return u.user_id < current.author;
}

bool apply_if_wins(Editor &e, const Update &u) {
    if (u.line < 0) return false;
    if (u.line >= (int)e.meta.size() || update_wins(u, e.meta[u.line])) {
        apply_update(e, u);
        return true;
    }
    return false;
}

int apply_all(Editor &e, const vector<Update> &updates) {
    int applied = 0;
    for (const auto &u : updates) {
        if (apply_if_wins(e, u)) applied++;
    }
    return applied;
}
