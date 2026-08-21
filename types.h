// Shared types for SyncText.
// Every module includes this; nothing here does I/O.

#ifndef SYNCTEXT_TYPES_H
#define SYNCTEXT_TYPES_H

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <chrono>
#include <mqueue.h>

inline constexpr int MAX_USERS = 5;
inline constexpr int USER_ID_LEN = 32;
inline constexpr int MQ_NAME_LEN = 64;
inline constexpr int MQ_MSG_SIZE = 2048;
inline constexpr const char *SHM_NAME = "/sync_registry_v1";

// Who is online. Lives in POSIX shared memory so every process sees the same copy.
struct Registry {
    int slot_in_use[MAX_USERS];
    char user_id[MAX_USERS][USER_ID_LEN];
    char mq_name[MAX_USERS][MQ_NAME_LEN];
};

// One line change. This is the CRDT operation we send over the message queue.
struct Update {
    std::string op;          // "insert", "replace", or "delete"
    int line = 0;
    int col_start = 0;       // first changed column (for the UI / demo)
    int col_end = 0;
    std::string old_text;
    std::string new_text;
    long long timestamp = 0;
    std::string user_id;
};

// Last-write-wins metadata stored next to each line in memory.
struct LineMeta {
    long long timestamp = 0;
    std::string author;
};

// Everything one editor process needs. Passed by reference into each module.
struct Editor {
    std::string user_id;
    std::string mq_name;
    std::string doc_path;

    int shm_fd = -1;
    Registry *registry = nullptr;
    int slot = -1;
    mqd_t mq = (mqd_t)-1;

    std::mutex incoming_mu;
    std::queue<Update> incoming;

    std::vector<std::string> lines;  // in-memory document
    std::vector<LineMeta> meta;      // LWW clock per line

    std::atomic<bool> running{true};
};

inline long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

#endif
