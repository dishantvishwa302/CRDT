// Shared memory registry: "who is online, and what is their queue name?"
// Slot claim uses compare-and-swap so two processes cannot take the same slot.

#include "registry.h"

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mqueue.h>

using namespace std;

bool attach_registry(Editor &e) {
    e.shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (e.shm_fd == -1) {
        perror("shm_open");
        return false;
    }
    if (ftruncate(e.shm_fd, sizeof(Registry)) == -1) {
        perror("ftruncate");
        return false;
    }

    e.registry = (Registry *)mmap(nullptr, sizeof(Registry),
                                  PROT_READ | PROT_WRITE, MAP_SHARED, e.shm_fd, 0);
    if (e.registry == MAP_FAILED) {
        perror("mmap");
        e.registry = nullptr;
        return false;
    }

    // Fresh shm can contain garbage. If flags are not 0/1, wipe the table.
    bool garbage = false;
    for (int i = 0; i < MAX_USERS; i++) {
        if (e.registry->slot_in_use[i] != 0 && e.registry->slot_in_use[i] != 1)
            garbage = true;
    }
    if (garbage) {
        memset(e.registry, 0, sizeof(Registry));
    }
    return true;
}

void detach_registry(Editor &e) {
    if (e.registry) {
        munmap(e.registry, sizeof(Registry));
        e.registry = nullptr;
    }
    if (e.shm_fd != -1) {
        close(e.shm_fd);
        e.shm_fd = -1;
    }
}

static void clear_slot(Registry *reg, int i) {
    reg->slot_in_use[i] = 0;
    memset(reg->user_id[i], 0, USER_ID_LEN);
    memset(reg->mq_name[i], 0, MQ_NAME_LEN);
}

int register_user(Editor &e) {
    Registry *reg = e.registry;

    // Drop users whose message queue is gone (crashed without cleanup).
    for (int i = 0; i < MAX_USERS; i++) {
        if (!reg->slot_in_use[i]) continue;
        mqd_t mq = mq_open(reg->mq_name[i], O_WRONLY | O_NONBLOCK);
        if (mq == (mqd_t)-1) {
            clear_slot(reg, i);
        } else {
            mq_close(mq);
        }
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (reg->slot_in_use[i] && e.user_id == reg->user_id[i]) {
            cerr << "User id already registered: " << e.user_id << "\n";
            return -1;
        }
    }

    // Atomically claim a free slot (0 -> 1).
    for (int i = 0; i < MAX_USERS; i++) {
        int expected = 0;
        if (__sync_bool_compare_and_swap(&reg->slot_in_use[i], expected, 1)) {
            strncpy(reg->user_id[i], e.user_id.c_str(), USER_ID_LEN - 1);
            reg->user_id[i][USER_ID_LEN - 1] = '\0';
            strncpy(reg->mq_name[i], e.mq_name.c_str(), MQ_NAME_LEN - 1);
            reg->mq_name[i][MQ_NAME_LEN - 1] = '\0';
            return i;
        }
    }
    return -1;
}

void unregister_user(Editor &e) {
    if (!e.registry || e.slot < 0 || e.slot >= MAX_USERS) return;
    clear_slot(e.registry, e.slot);
    e.slot = -1;
}

vector<string> active_users(const Editor &e) {
    vector<string> users;
    if (!e.registry) return users;
    for (int i = 0; i < MAX_USERS; i++) {
        if (e.registry->slot_in_use[i])
            users.push_back(e.registry->user_id[i]);
    }
    return users;
}

void print_active(const Editor &e) {
    auto users = active_users(e);
    cout << "Active (" << users.size() << "): ";
    for (size_t i = 0; i < users.size(); i++) {
        if (i) cout << ", ";
        cout << users[i];
    }
    cout << "\n";
}
