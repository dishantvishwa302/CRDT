// SyncText — collaborative editor on one machine (POSIX shm + message queues + LWW).
//
// Usage:  ./editor user1
// Edit user1_doc.txt in vim/nano. Open another terminal with ./editor user2.

#include "types.h"
#include "registry.h"
#include "ipc.h"
#include "monitor.h"
#include "display.h"

#include <iostream>
#include <cctype>
#include <csignal>
#include <thread>
#include <functional>
#include <cstring>

using namespace std;

static Editor *g_editor = nullptr;

static void on_sigint(int) {
    if (g_editor) g_editor->running = false;
}

static bool valid_user_id(const string &id) {
    if (id.empty() || id.size() >= (size_t)USER_ID_LEN) return false;
    for (char c : id) {
        if (!isalnum((unsigned char)c) && c != '_') return false;
    }
    return true;
}

static void cleanup(Editor &e) {
    close_inbox(e);
    unregister_user(e);
    detach_registry(e);
    restoreCursor();
    cout << "Cleanup done\n";
}

int main(int argc, char **argv) {
    if (argc < 2) {
        cerr << "Usage: ./editor <user_id>\n";
        return 1;
    }

    Editor e;
    e.user_id = argv[1];
    e.mq_name = "/queue_" + e.user_id;
    e.doc_path = e.user_id + "_doc.txt";

    if (!valid_user_id(e.user_id)) {
        cerr << "user_id must be alphanumeric (and _)\n";
        return 1;
    }

    g_editor = &e;
    signal(SIGINT, on_sigint);

    if (!attach_registry(e)) return 1;

    // Create the queue BEFORE claiming a registry slot. Otherwise another
    // process can see "slot in use, queue missing" and wipe us as stale.
    if (!open_inbox(e)) {
        detach_registry(e);
        return 1;
    }

    e.slot = register_user(e);
    if (e.slot == -1) {
        cerr << "Could not register (too many users, or id already in use)\n";
        close_inbox(e);
        detach_registry(e);
        return 1;
    }
    cout << "Registered " << e.user_id << " in slot " << e.slot << "\n";
    print_active(e);

    init_doc(e.doc_path);

    thread listener_thread(listener, ref(e));
    monitor_loop(e);

    e.running = false;
    listener_thread.join();
    cleanup(e);
    return 0;
}
