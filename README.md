# SyncText

Same-machine collaborative text editor. Several users each run `./editor <id>`, edit their own `*_doc.txt` in vim/nano, and see each other's line changes in the terminal.

Conflict rule: **Last-Write-Wins per line** (later timestamp wins; same timestamp → smaller user id wins).

## Build

```bash
make
```

## Run

Open two terminals:

```bash
./editor user1
./editor user2
```

Edit `user1_doc.txt` in another editor. Within about a second, user2's terminal (and `user2_doc.txt`) should update.

Ctrl+C unregisters you and removes your message queue.

If a process crashed and slots look stuck:

```bash
rm -f /dev/shm/sync_registry_v1
rm -f /dev/mqueue/queue_*
```

## Layout

| File | Role |
|------|------|
| `main.cpp` | Start, register, listener thread, cleanup |
| `registry.cpp` | Shared memory: who is online |
| `ipc.cpp` | Message queues + serialize an `Update` |
| `crdt.cpp` | LWW merge |
| `monitor.cpp` | `stat()` the file, diff, broadcast |
| `display.cpp` | Terminal view |

See `GUIDE.md` for what changed from the original, and `DESIGNDOC.md` for how it works.
