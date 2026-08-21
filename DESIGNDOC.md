# SyncText Design

Collaborative editor for several processes on **one Linux machine**. No network, no central server. Each process is a peer.

You edit a normal text file (`user1_doc.txt`). The process watches that file, sends line changes to the others, and applies remote changes with Last-Write-Wins.

---

## Architecture

```
  ./editor user1                  ./editor user2
  monitors user1_doc.txt          monitors user2_doc.txt
           |                               |
           |         shared memory         |
           +---- /sync_registry_v1 --------+
                 (ids + queue names)
           |                               |
           +---- POSIX message queues -----+
              /queue_user1  /queue_user2
```

Two IPC tools:

1. **Shared memory** — directory of who is online.
2. **Message queues** — the actual edits.

---

## Startup

1. `attach_registry` — `shm_open` + `mmap` the registry.
2. `open_inbox` — create `/queue_<user_id>` **before** registering, so a peer does not treat you as crashed.
3. `register_user` — drop crashed users (their queue is gone), then **CAS** a free slot (`0 → 1`) so two processes cannot take the same slot.
4. Start a **listener** thread; the main thread runs **monitor**.
5. Ctrl+C sets `running = false`, joins the listener, unregisters, unlinks the queue.

Max 5 users. Ids must be alphanumeric.

---

## Local edits (`monitor.cpp`)

Every ~1s the monitor calls `stat()` on the file.

If mtime or size changed:

1. Read the file, compare to the in-memory copy, line by line.
2. For each changed line, build an `Update` (insert / replace / delete).
3. `diff_columns` finds the changed span (common prefix + suffix) — useful to show, not used by merge.
4. Stamp that line's LWW metadata with `now_ms()` and **send the update immediately** to every other queue.

The terminal does not take typing. Edit the file in vim, nano, etc.

---

## Remote edits (`ipc.cpp` + `crdt.cpp`)

The listener thread does non-blocking `mq_receive`. Each message becomes an `Update` and is pushed onto a `mutex` + `queue`.

The monitor drains that queue and calls `apply_if_wins`:

- Each line is an **LWW-Register**: `(timestamp, user_id)`.
- Incoming wins if its timestamp is larger, or timestamps are equal and its `user_id` is lexicographically smaller.
- Order of apply does not matter; everyone keeps the same winner.

Then the replica is written back to the file. `mtime` is refreshed so we do not treat our own write as a local edit.

---

## `Update` on the wire

Pipe-separated, `|` in text escaped as `<P>`, newlines as `<N>`:

```
replace|2|0|7|1710000000000|user1|old line|new line
```

---

## Threads

| Thread | Work |
|--------|------|
| Main (`monitor_loop`) | `stat`, diff, send, merge, draw |
| Listener | `mq_receive` → incoming queue |

They share only the incoming queue (mutex). The replica is touched only by the monitor.

---

## Limits (say this in an interview)

- **Line-based**, not character-based. Two people editing the same line: one line survives.
- Inserting a line shifts later line numbers; those can “conflict” with someone else's edit of the old line N.
- Same machine only (POSIX IPC).
- Trailing deleted lines become empty lines on peers that still have that index.

That is enough for a demo of CRDT convergence + syscalls. It is not Google Docs.
