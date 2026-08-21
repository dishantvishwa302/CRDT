# GUIDE — What was here, and what changed

This is a note for you (not a user manual).

---

## Original project

SyncText: several processes on one machine edit copies of a document and merge with a **Last-Write-Wins CRDT**.

IPC was already the right story:

- POSIX **shared memory** = who is online
- POSIX **message queues** = send line updates
- **LWW per line** = conflict rule

Almost all of that lived in one dense `main.cpp`, plus a lock-free queue, send-batching, and a 575-line design doc that described more machinery than the demo needed.

`statement.pdf` in this folder is the **MyTerm** lab PDF, not this project. Ignore it here.

---

## What you asked for this time

Split the code so you can walk through it. Drop the lock-free queue. Send every edit immediately. Keep file-based editing (no typing in the terminal). Trim the docs. Add a Makefile.

---

## How it works now

```
./editor user1
    → shm_open registry, CAS a slot
    → mq_open /queue_user1
    → listener thread: mq_receive → mutex queue
    → monitor: stat(user1_doc.txt)
         local change → Update → send to every other queue
         remote Update → LWW per line → write file → redraw
```

You type in **vim/nano** on `user1_doc.txt`. The SyncText window only shows the document and a short activity log.

---

## What changed in this pass

| Before | After |
|--------|--------|
| One 450-line `main.cpp` | `main` / `registry` / `ipc` / `crdt` / `monitor` / `display` |
| Custom lock-free ring (`LFQueue`) | `mutex` + `std::queue` |
| `localbuf` + `BROADCAST_BATCH` + `MERGE_BATCH` | Every local line change is sent immediately |
| Collect all ops, sort, then pick a winner | Each line is an LWW-Register; `apply_if_wins` |
| Two diff helpers (`diffcols` / `diff_columns_old`) | One `diff_columns` |
| Unused `apply_update` path, empty `display_init` / `clearScreen` | Removed |
| `<bits/stdc++.h>`, globals, `exit()` from SIGINT | Explicit includes, `Editor` struct, `running` flag + join |
| Compile line in a comment | `make` |
| DESIGNDOC ~575 lines | DESIGNDOC matches this code |

Unchanged on purpose: shared memory registry, CAS slot claim, message queues, line-based LWW, `stat()` polling, color terminal view.

Also fixed a real bug: the original registered in shared memory **before** creating the message queue. A second process starting at the same time would see “slot taken, queue missing,” wipe that slot, and both users could end up in slot 0. The queue is created first now.

---

## Files to walk through in an interview

1. `main.cpp` — “I register, start a listener, then watch the file.”
2. `registry.cpp` — “Shared memory is the directory. CAS so two users cannot take the same slot.”
3. `ipc.cpp` — “Each user has a queue. Broadcast is `mq_send` to everyone else.”
4. `crdt.cpp` — “Each line is last-write-wins. Later timestamp wins.”
5. `monitor.cpp` — “`stat` the file, diff lines, send, apply remotes.”

Two-minute pitch:

> SyncText is a collaborative editor for processes on one machine. Shared memory lists who is online. Each process has a POSIX message queue. When I edit my file, I diff it line by line, timestamp the change, and send it. Each line is a last-write-wins register, so if two people edit the same line, the later timestamp wins and every replica converges. The terminal only displays; I edit the file in a normal editor.

---

## Demo

```bash
make
```

Terminal A: `./editor user1`  
Terminal B: `./editor user2`  
Terminal C: edit `user1_doc.txt` (change a line, save)

User2's terminal should redraw with `[MOD]` on that line, and `user2_doc.txt` should match.

Then edit the **same** line in `user2_doc.txt` and save. Both files keep the later save.

Ctrl+C in A and B. `make clean` removes the binary and `*_doc.txt`.

If a previous run crashed: `rm -f /dev/shm/sync_registry_v1 /dev/mqueue/queue_*`

---

## What this is not

Not a character-level CRDT (RGA/Logoot). Not network collaboration. Not an in-terminal editor. Line inserts shift later indices, so this is a teaching LWW, not Google Docs.
