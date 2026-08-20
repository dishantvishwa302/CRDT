# SyncText Design Document
---

## Table of Contents
1.  [**Introduction**](#1-introduction)
    1.1. [Purpose of the Document](#11-purpose-of-the-document)
    1.2. [Project Overview](#12-project-overview)
    1.3. [Key Features](#13-key-features)
    1.4. [Document Conventions](#14-document-conventions)
2.  [**System Architecture**](#2-system-architecture)
    2.1. [High-Level Overview](#21-high-level-overview)
    2.2. [Component Breakdown](#22-component-breakdown)
    2.3. [User Session Lifecycle](#23-user-session-lifecycle)
3.  [**Inter-Process Communication (IPC)**](#3-inter-process-communication-ipc)
    3.1. [Shared Memory for User Discovery](#31-shared-memory-for-user-discovery)
    3.2. [Message Queues for Update Propagation](#32-message-queues-for-update-propagation)
4.  [**CRDT-Based Synchronization**](#4-crdt-based-synchronization)
    4.1. [Chosen CRDT Model: Last-Write-Wins (LWW)](#41-chosen-crdt-model-last-write-wins-lww)
    4.2. [The `Update` Data Structure](#42-the-update-data-structure)
    4.3. [Timestamping](#43-timestamping)
    4.4. [Serialization and Deserialization](#44-serialization-and-deserialization)
    4.5. [The Merge Algorithm (`merge_and_apply`)](#45-the-merge-algorithm-merge_and_apply)
5.  [**Local Change Detection**](#5-local-change-detection)
    5.1. [Monitoring File Modifications (`monitor` function)](#51-monitoring-file-modifications-monitor-function)
    5.2. [Diffing Algorithm (`diff_columns_old`)](#52-diffing-algorithm-diff_columns_old)
    5.3. [Generating `Update` Objects](#53-generating-update-objects)
    5.4. [Broadcasting Logic and Batching](#54-broadcasting-logic-and-batching)
6.  [**Internal Buffering**](#6-internal-buffering)
    6.1. [The `LFQueue` (Lock-Free Queue)](#61-the-lfqueue-lock-free-queue)
    6.2. [`localbuf` and `recvbuf`](#62-localbuf-and-recvbuf)
7.  [**User Interface (UI)**](#7-user-interface-ui)
    7.1. [UI Components](#71-ui-components)
    7.2. [Color-Coded Output](#72-color-coded-output)
    7.3. [Thread Safety](#73-thread-safety)
8.  [**Concurrency Model**](#8-concurrency-model)
    8.1. [Main Thread (`monitor`)](#81-main-thread-monitor)
    8.2. [Listener Thread (`listener`)](#82-listener-thread-listener)
    8.3. [Thread Interaction and Data Flow](#83-thread-interaction-and-data-flow)
9.  [**Code and Results**](#9-code-and-results)
    9.1. [Compiling and Running](#91-compiling-and-running)
    9.2. [Example Scenario: Concurrent Editing](#92-example-scenario-concurrent-editing)
    9.3. [Code Snippet: `merge_and_apply`](#93-code-snippet-merge_and_apply)
    9.4. [Code Snippet: `monitor`](#94-code-snippet-monitor)
10. [**Future Work and Potential Improvements**](#10-future-work-and-potential-improvements)
    10.1. [Advanced CRDTs](#101-advanced-crdts)
    10.2. [Network-Based Collaboration](#102-network-based-collaboration)
    10.3. [Richer Text Formatting](#103-richer-text-formatting)
    10.4. [Cursor Position Sharing](#104-cursor-position-sharing)
    10.5. [Improved UI/UX](#105-improved-uiux)

---

## 1. Introduction

### 1.1. Purpose of the Document
This document provides a detailed technical design and architectural overview of **SyncText**, a command-line based collaborative text editor. It is intended for developers, contributors, and anyone interested in understanding the internal workings of the system, from its high-level architecture down to its specific implementation details.

### 1.2. Project Overview
SyncText enables multiple users on the same machine to edit a text document concurrently. It ensures that all users have a consistent view of the document, even when edits are made simultaneously. This is achieved through a decentralized architecture and a **Conflict-free Replicated Data Type (CRDT)**.

The core design philosophy is to create a simple, robust, and easy-to-understand collaborative system using standard POSIX IPC mechanisms, making it a practical example of CRDTs in action.

### 1.3. Key Features
- **Real-time Collaboration:** Changes made by one user are propagated to all other users in near real-time.
- **Conflict-free Merging:** A **Last-Write-Wins (LWW)** CRDT is used to automatically and deterministically resolve conflicting edits without requiring user intervention.
- **Decentralized Architecture:** Each editor instance is a peer; there is no central server, which eliminates a single point of failure.
- **Multi-user Support:** The system is designed to support up to 5 concurrent users on the same machine.
- **Simple Terminal UI:** A clean, terminal-based interface shows the document content with color-coded changes and a summary of recent activity.

### 1.4. Document Conventions
- `monospace`: Used for code, filenames, commands, and data structure names.
- **Bold**: Used for emphasis and key terms.
- `[USER_ID]`: A placeholder for a user-specific identifier.

---

## 2. System Architecture

### 2.1. High-Level Overview
SyncText operates on a decentralized, peer-to-peer model. Each user runs a separate instance of the editor application. These instances communicate directly with each other using two primary IPC mechanisms: **shared memory** for discovering other users and **POSIX message queues** for sending and receiving updates.

The following diagram illustrates the high-level architecture:

```
+--------------------------------------------------------------------------+
|                            Local Machine                                 |
|                                                                          |
|  +-----------------+      +-----------------+      +-----------------+   |
|  | Editor (user1)  |      | Editor (user2)  |      | Editor (user3)  |   |
|  +-------+---------+      +--------+--------+      +--------+--------+   |
|          |                      |                       |              |
|          |                      |                       |              |
|  +-------v----------------------v-----------------------v--------+     |
|  |                                                              |     |
|  |                Shared Memory (SHM) - "/sync_registry_v1"     |     |
|  |             (User Registry: IDs and MQ Names)                |     |
|  |                                                              |     |
|  +--------------------------------------------------------------+     |
|          ^                      ^                       ^              |
|          |                      |                       |              |
|  +-------+---------+      +-----+-----------+      +----+------------+ |
|  | MQ (/q_user1)   <------+   MQ (/q_user2)   <------+ MQ (/q_user3)  | |
|  | (Receives Updates)     | (Receives Updates)      | (Receives Updates)| |
|  +-----------------+      +-----------------+      +-----------------+ |
|                                                                          |
+--------------------------------------------------------------------------+
```
**Figure 1: System Architecture Diagram**

### 2.2. Component Breakdown

#### 2.2.1. Editor Instance
Each user runs a standalone editor process. This process is responsible for:
- **User Registration:** Announcing its presence to other users.
- **File Monitoring:** Detecting local changes made by the user to their document file.
- **Change Broadcasting:** Sending local changes to all other active users.
- **Update Listening:** Receiving changes from other users.
- **CRDT Merging:** Applying local and remote changes to maintain a consistent document state.
- **UI Rendering:** Displaying the document and activity summary in the terminal.

#### 2.2.2. Shared Memory Registry
A single POSIX shared memory segment, identified by the name `/sync_registry_v1`, acts as a central directory for user discovery. It contains a `Registry` struct that holds information about all active users, including their user IDs and message queue names. This allows a new editor instance to find and communicate with its peers.

#### 2.2.3. POSIX Message Queues
Each editor instance creates a unique POSIX message queue for receiving updates. The name of the queue is derived from the user's ID (e.g., `/queue_user1`). When a user makes a change, their editor broadcasts it by sending a message to the message queues of all other registered users. This provides a reliable, asynchronous, and many-to-one communication channel.

### 2.3. User Session Lifecycle

#### 2.3.1. Startup and Registration
1.  On startup, the editor process takes a `user_id` as a command-line argument.
2.  It attaches to the shared memory segment (`attach_reg`).
3.  It performs a cleanup of any stale user slots in the registry.
4.  It attempts to atomically register itself in an available slot using a Compare-and-Swap (CAS) operation (`reg_user`).
5.  If successful, it creates its own message queue and starts a `listener` thread to monitor it.
6.  It initializes a local document file (e.g., `user1_doc.txt`) if one doesn't exist.
7.  It enters the main `monitor` loop.

#### 2.3.2. Editing and Synchronization
- The user edits their local document file using any external text editor.
- The `monitor` loop detects the file change (via `stat`).
- It computes the difference and creates an `Update` object.
- The `Update` is serialized and broadcast to all other users' message queues (`send_all`).
- Concurrently, the `listener` thread receives `Update` messages from other users and places them in a lock-free queue (`incoming`).
- The `monitor` loop periodically checks the `incoming` queue and triggers the `merge_and_apply` function to resolve conflicts and update the local document.

#### 2.3.3. Shutdown and Deregistration
1.  When the user presses `Ctrl+C`, a `SIGINT` signal is caught.
2.  The `cleanup` function is called.
3.  The editor removes its entry from the shared memory registry (`unreg_user`).
4.  It closes and unlinks its message queue.
5.  It detaches from the shared memory segment.
6.  The process exits.

---

## 3. Inter-Process Communication (IPC)

### 3.1. Shared Memory for User Discovery
The shared memory segment is the backbone of user discovery in SyncText's decentralized architecture.

#### 3.1.1. `Registry` Data Structure
The structure of the shared memory is defined by the `Registry` struct:
```cpp
struct Registry {
    int slot_in_use[MAX_USERS];
    char user_id[MAX_USERS][USER_ID_LEN];
    char mq_name[MAX_USERS][MQ_NAME_LEN];
};
```
- `slot_in_use`: An array of integers (0 or 1) acting as flags to indicate if a user slot is occupied.
- `user_id`: Stores the unique identifier for the user in each slot.
- `mq_name`: Stores the name of the message queue for the user in each slot.

#### 3.1.2. Atomic Registration (`__sync_bool_compare_and_swap`)
To prevent race conditions where two users might try to claim the same slot simultaneously, registration is performed atomically. The `reg_user` function iterates through the slots and uses the GCC built-in `__sync_bool_compare_and_swap` to safely acquire a lock-free slot.

```cpp
// Simplified logic from reg_user
for (int i = 0; i < MAX_USERS; i++) {
    int expected = 0;
    // Atomically check if slot_in_use[i] is 0 and, if so, set it to 1.
    if (__sync_bool_compare_and_swap(&regptr->slot_in_use[i], expected, 1)) {
        // Success, claim the slot
        strncpy(regptr->user_id[i], uid.c_str(), USER_ID_LEN - 1);
        strncpy(regptr->mq_name[i], mqname.c_str(), MQ_NAME_LEN - 1);
        return i; // Return the claimed slot index
    }
}
```

#### 3.1.3. Stale User Cleanup
If an editor process crashes without cleaning up, its slot in the registry becomes "stale." To handle this, before a new user registers, the system checks the validity of existing users. It does this by attempting to open each registered user's message queue. If `mq_open` fails, it assumes the user is stale and clears their slot in the registry.

### 3.2. Message Queues for Update Propagation
Once users are discovered via shared memory, message queues handle the actual communication of document changes.

#### 3.2.1. Queue Naming Convention
Each user's message queue is given a unique, predictable name based on their user ID. For a user `user1`, the queue name is `/queue_user1`. This convention allows any other process to easily construct the queue name and send messages to it.

#### 3.2.2. Asynchronous Communication
Message queues are inherently asynchronous. When an editor broadcasts an update, `mq_send` places the message in the recipient's queue and returns immediately (when used in `O_NONBLOCK` mode). The recipient can retrieve the message at its own pace. This decouples the sender from the receiver, improving system responsiveness.

#### 3.2.3. `listener` Thread
A dedicated thread is spawned for the sole purpose of listening for incoming messages.
```cpp
void listener(const string&mqn) {
    // ... setup mq_open ...
    char buf[2048];
    while (true) {
        ssize_t b = mq_receive(mqd, buf, sizeof(buf) - 1, nullptr);
        if (b >= 0) {
            buf[b] = '\0';
            Update u;
            if (deser(buf, u)) {
                incoming.push(u); // Push to lock-free queue
            }
        }
        // ... sleep on EAGAIN ...
    }
}
```
This design ensures that the main application thread is never blocked waiting for I/O. It can continue monitoring local file changes while the `listener` concurrently handles remote updates.

---

## 4. CRDT-Based Synchronization

### 4.1. Chosen CRDT Model: Last-Write-Wins (LWW)
SyncText uses a simple and effective CRDT strategy: **Last-Write-Wins (LWW)**. In this model, every change (`Update`) is associated with a timestamp. If two updates conflict (i.e., they modify the same line of the document), the update with the later timestamp is the one that "wins" and is ultimately applied. To handle cases where timestamps are identical, the `user_id` is used as a deterministic tie-breaker.

This approach guarantees **eventual consistency**: all users will converge to the same document state, regardless of the order in which they receive updates.

### 4.2. The `Update` Data Structure
An `Update` represents a single, atomic change to the document.
```cpp
struct Update {
    string op;
    int line, col_start, col_end;
    string old_text, new_text;
    long long timestamp;
    string user_id;
};
```
- `op`: The type of operation: `"insert"`, `"delete"`, or `"replace"`.
- `line`: The line number affected by the change.
- `col_start`, `col_end`: The column range of the change within the line.
- `old_text`, `new_text`: The content of the line before and after the change.
- `timestamp`: A millisecond-resolution Unix timestamp.
- `user_id`: The ID of the user who made the change.

### 4.3. Timestamping
Timestamps are crucial for the LWW model. A high-resolution timestamp is generated using `std::chrono`.
```cpp
long long now_ms() {
    using namespace chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
```

### 4.4. Serialization and Deserialization
To send an `Update` struct over a message queue, it must be converted into a byte stream (a string).

#### 4.4.1. `ser` and `deser` functions
- `ser(const Update& u)`: Serializes an `Update` struct into a pipe-delimited (`|`) string.
- `deser(const string& s, Update& o)`: Deserializes the string back into an `Update` struct.

Example serialized string:
`replace|2|0|23|"Welcome to SyncText"|"Hello Collaborative World"|1678886400000|user1`

#### 4.4.2. Character Escaping (`esc`, `unesc`)
Since the pipe character `|` is used as a delimiter, any `|` characters within the text content must be escaped to avoid parsing errors. The `esc` function replaces `|` with `<P>` and newlines with `<N>`. The `unesc` function reverses this process.

### 4.5. The Merge Algorithm (`merge_and_apply`)
This function is the heart of the CRDT implementation. It resolves conflicts and brings the document to a consistent state.

#### 4.5.1. Gathering Local and Remote Updates
The function collects all pending updates from two sources:
- `localbuf`: Updates generated from the user's own file edits.
- `recvbuf`: Updates received from other users (drained from the `incoming` queue).

#### 4.5.2. Deterministic Sorting
All collected updates are sorted to ensure that every user processes them in the exact same order. The primary sort key is the `timestamp`, and the secondary key is the `user_id` for tie-breaking.
```cpp
sort(all.begin(), all.end(), [](const Update &a, const Update &b) {
    if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
    return a.user_id < b.user_id;
});
```

#### 4.5.3. Conflict Resolution Logic
The algorithm iterates through the sorted updates and uses a map to store the "winning" update for each line.
```cpp
unordered_map<int, Update> chosen;
for (auto &upd : all) {
    int ln = upd.line;
    // If no update for this line yet, or if the new update is "later",
    // it becomes the winner for this line.
    if (chosen.find(ln) == chosen.end() || is_later(upd, chosen[ln])) {
        chosen[ln] = upd;
    }
}
```
This effectively applies the LWW rule: for each line, only the last write is kept.

#### 4.5.4. Applying Final Updates
After the winning update for each affected line has been determined, they are applied to the document in line-number order to prevent document resizing issues. The modified document is then written back to the file.

---

## 5. Local Change Detection

### 5.1. Monitoring File Modifications (`monitor` function)
The `monitor` function runs in an infinite loop in the main thread. It periodically checks the metadata of the user's document file using `stat()`.
```cpp
struct stat sb;
stat(fname.c_str(), &sb);
time_t mt = sb.st_mtime;
off_t last_size = sb.st_size;

while (true) {
    this_thread::sleep_for(chrono::seconds(2));
    if (stat(fname.c_str(), &sb) == -1) continue;

    if (sb.st_mtime != mt || sb.st_size != last_size) {
        // File has changed, process it.
    }
}
```
A change is detected if either the modification time (`st_mtime`) or the file size (`st_size`) has changed since the last check.

### 5.2. Diffing Algorithm (`diff_columns_old`)
When a file change is detected, the application reads the new content and compares it line-by-line with its last known state. For lines that have changed, the `diff_columns_old` function finds the start and end columns of the modification. It does this by finding the longest common prefix and suffix of the old and new lines.

### 5.3. Generating `Update` Objects
For each modified line, an `Update` object is created, populated with the line number, diff info, old and new text, a new timestamp, and the user's ID. The `op` is determined based on whether the old or new line is empty.

### 5.4. Broadcasting Logic and Batching
To avoid flooding the network with too many small messages, updates are batched.
- If only a single line change is detected, it is sent immediately for responsiveness.
- If multiple lines change at once (e.g., from a copy-paste), they are added to `localbuf`.
- When `localbuf` reaches a threshold (`BROADCAST_BATCH`), all updates in the buffer are serialized and sent.

---

## 6. Internal Buffering

### 6.1. The `LFQueue` (Lock-Free Queue)
A lock-free queue is used to safely pass `Update` objects from the `listener` thread to the main `monitor` thread without using mutexes, which could cause priority inversion or deadlocks.

#### 6.1.1. Purpose and Design
The queue is implemented as a circular buffer. It has two atomic indices: `head` and `tail`.
- The `listener` thread is the **producer**: it writes to `tail` and increments it.
- The `monitor` thread is the **consumer**: it reads from `head` and increments it.

#### 6.1.2. Implementation (Circular Buffer with Atomic Head/Tail)
```cpp
class LFQueue {
    Update* buf; size_t cap;
    atomic<size_t> head, tail;
public:
    LFQueue() { /* ... */ }
    bool push(const Update& u) {
        size_t t = tail.load();
        size_t n = (t + 1) & (cap - 1); // Next position
        if (n == head.load()) return false; // Full
        buf[t] = u;
        tail.store(n); // Publish
        return true;
    }
    bool pop(Update& u) {
        size_t h = head.load();
        if (h == tail.load()) return false; // Empty
        u = buf[h];
        head.store((h + 1) & (cap - 1)); // Consume
        return true;
    }
};
```
The use of `std::atomic` ensures that `head` and `tail` are updated atomically, making the queue safe for single-producer, single-consumer scenarios without explicit locks.

### 6.2. `localbuf` and `recvbuf`
- `vector<Update> localbuf`: A buffer in the main thread to accumulate local changes before they are broadcast in a batch.
- `vector<Update> recvbuf`: A buffer in the main thread to accumulate remote changes drained from the `incoming` queue before they are processed by the merge algorithm.

---

## 7. User Interface (UI)

### 7.1. UI Components
The UI is handled by the functions in `display.cpp` and `display.h`. It is a simple, text-based interface rendered in the terminal. The two main components are:
- **Document View:** Displays the entire content of the document, with line numbers.
- **Summary View:** Shows a log of recent activities, such as merged updates.

### 7.2. Color-Coded Output
To improve readability, the UI uses ANSI escape codes to color-code the output.
- **Green (`[NEW]`):** A new line has been added.
- **Yellow (`[MOD]`):** An existing line has been modified.
- **Red (`[DEL]`):** A line has been deleted.
- **Cyan:** Line numbers.
- **Magenta:** Document title.
- **Blue:** Summary title.

### 7.3. Thread Safety
Since the main thread (running `monitor`) and potentially other parts of the application might print to the console, all printing is protected by a `std::mutex` (`print_mutex`) to prevent interleaved or corrupted output.

---

## 8. Concurrency Model

The application's concurrency is managed across two main threads.

### 8.1. Main Thread (`monitor`)
The primary thread of execution. Its responsibilities include:
- Initializing the user session.
- Running the `monitor` loop to detect local file changes.
- Generating and broadcasting local `Update`s.
- Draining the `incoming` queue into `recvbuf`.
- Triggering and executing the `merge_and_apply` logic.
- Refreshing the terminal UI.

### 8.2. Listener Thread (`listener`)
A secondary thread dedicated to handling network I/O. Its responsibilities are:
- Blocking on `mq_receive` to wait for new messages.
- Deserializing received messages into `Update` objects.
- Pushing the `Update` objects into the `incoming` lock-free queue.

### 8.3. Thread Interaction and Data Flow
The data flows in one primary direction between the threads:

```
+-----------------+   (mq_receive)   +-----------+   (push)   +-----------+   (pop)   +-------------+
| Other Editors   | ---------------> | Listener  |----------->| LFQueue   |---------->| Main Thread |
| (via Msg Queue) |                  | Thread    |            | (incoming)|           | (monitor)   |
+-----------------+                  +-----------+            +-----------+           +-------------+
```
**Figure 2: Thread Interaction Data Flow**

This separation of concerns ensures that the UI and local file monitoring remain responsive even when there is a high volume of incoming remote updates.

---

## 9. Code and Results

### 9.1. Compiling and Running
The project can be compiled with a single g++ command:
```bash
g++ main.cpp display.cpp -o editor -std=c++17 -lpthread -lrt
```
- `-std=c++17`: Required for modern C++ features.
- `-lpthread`: Links the POSIX threads library.
- `-lrt`: Links the real-time library, required for shared memory and message queues.

To run, open multiple terminals and execute:
```bash
# Terminal 1
./editor user1

# Terminal 2
./editor user2
```

### 9.2. Example Scenario: Concurrent Editing
1.  **Initial State:** Both `user1_doc.txt` and `user2_doc.txt` contain "Hello World".
2.  **user1's Edit:** User1 changes the line to "Hello C++ World".
    - `monitor` on user1's instance detects the change.
    - An `Update` is created and broadcast to `/queue_user2`.
3.  **user2's Edit:** At the same time, User2 changes the line to "Hello CRDT World".
    - `monitor` on user2's instance detects the change.
    - An `Update` is created and broadcast to `/queue_user1`.
4.  **Conflict and Merge:**
    - Both editors receive the other's update.
    - The `merge_and_apply` function is triggered on both sides.
    - Let's assume user2's edit had a slightly later timestamp.
    - The LWW rule dictates that user2's change ("Hello CRDT World") wins.
    - Both `user1_doc.txt` and `user2_doc.txt` will converge to have the content "Hello CRDT World".

### 9.3. Code Snippet: `merge_and_apply`
This function is the core of the conflict resolution logic.
```cpp
void merge_and_apply(const string &fname) {
    vector<Update> all;
    all.reserve(localbuf.size() + recvbuf.size());
    all.insert(all.end(), localbuf.begin(), localbuf.end());
    all.insert(all.end(), recvbuf.begin(), recvbuf.end());

    if (all.empty()) return;

    // Sort deterministically (timestamp -> user_id)
    sort(all.begin(), all.end(), [](const Update &a, const Update &b) {
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
        return a.user_id < b.user_id;
    });

    // LWW per line: find the winning update for each line
    unordered_map<int, Update> chosen;
    for (auto &upd : all) {
        chosen[upd.line] = upd; // Simple LWW: last one in sorted list wins
    }

    // Apply winning updates in line order
    auto doc = read_lines(fname);
    vector<pair<int, Update>> sortedIndex(chosen.begin(), chosen.end());
    sort(sortedIndex.begin(), sortedIndex.end(), /* sort by line number */);

    for (auto &p : sortedIndex) {
        apply_update(doc, p.second);
    }

    write_lines(fname, doc);
    // ... update UI ...
    localbuf.clear();
    recvbuf.clear();
}
```

### 9.4. Code Snippet: `monitor`
This function drives local change detection and triggers remote merges.
```cpp
void monitor(const string &fname) {
    auto last = read_lines(fname);
    time_t mt = 0;
    // ... get initial modification time ...

    while (true) {
        this_thread::sleep_for(chrono::seconds(2));

        // 1. LOCAL CHANGE DETECTION
        if (file_was_modified) {
            auto now = read_lines(fname);
            // ... diff `last` and `now` to generate updates ...
            // ... broadcast updates ...
            last = now;
            // ... refresh UI ...
        }

        // 2. REMOTE UPDATE COLLECTION
        Update x;
        while (incoming.pop(x)) {
            recvbuf.push_back(x);
        }

        // 3. MERGE TRIGGER
        if (!recvbuf.empty()) {
            merge_and_apply(fname);
            // ... refresh UI after merge ...
            last = read_lines(fname); // Update `last` state after merge
        }
    }
}
```

---

## 10. Future Work and Potential Improvements

### 10.1. Advanced CRDTs
The current LWW-per-line model is simple but has limitations (e.g., the "lost update" problem within a single line). Future versions could implement more granular, character-level CRDTs like:
- **RGA (Replicated Growable Array)**
- **Logoot** or **LSEQ**
These would allow for true concurrent text insertion and deletion within the same line without conflict.

### 10.2. Network-Based Collaboration
The current implementation is limited to a single machine using IPC. A major extension would be to replace the IPC layer with a network protocol (e.g., TCP/IP with a simple gossip protocol) to allow collaboration over the internet.

### 10.3. Richer Text Formatting
The model could be extended to support rich text by modifying the `Update` struct to include formatting information (e.g., bold, italics, color).

### 10.4. Cursor Position Sharing
To improve the collaborative experience, the editor could broadcast cursor positions. This would allow users to see where others are currently typing. This could be sent as a special, low-priority message type.

### 10.5. Improved UI/UX
While functional, the terminal UI is basic. It could be improved by:
- Using a library like **ncurses** for a more interactive, full-screen terminal application.
- Developing a graphical user interface (GUI) using a framework like Qt or wxWidgets.
- Displaying a list of active users directly in the UI.
