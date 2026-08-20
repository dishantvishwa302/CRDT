# SyncText: A CRDT-Based Collaborative Text Editor

SyncText is a command-line based collaborative text editor that allows multiple users to edit a document concurrently. It uses a Conflict-free Replicated Data Type (CRDT) approach to ensure that all users have a consistent view of the document, even with simultaneous edits.

## Features

- **Real-time Collaboration:** See changes from other users in real-time.
- **Conflict Resolution:** Uses a CRDT (Last-Write-Wins) to automatically resolve conflicting edits without requiring user intervention.
- **Multi-user Support:** Supports up to 5 concurrent users.
- **Simple UI:** A clean, terminal-based interface that shows the document content and a summary of recent activity.

## How it Works

The project uses a shared memory segment to maintain a registry of active users. Each user has a dedicated message queue for receiving updates from other users. When a user modifies the document, the changes are broadcast to all other active users.

The core of the conflict resolution is a CRDT based on the Last-Write-Wins (LWW) principle. Each edit is timestamped, and in case of a conflict, the edit with the later timestamp is chosen.

## How to Compile and Run

1.  **Compile the project:**
    ```bash
    g++ main.cpp display.cpp -o editor -std=c++17 -lpthread -lrt
    ```

2.  **Run the editor:**
    Open multiple terminals and run the editor with a unique user ID for each instance.

    *Terminal 1:*
    ```bash
    ./editor user1
    ```

    *Terminal 2:*
    ```bash
    ./editor user2
    ```

    ...and so on.

3.  **Start editing:**
    A file named `<user_id>_doc.txt` will be created for each user. You can open this file in your favorite text editor and start making changes. The changes will be reflected in the terminals of all other users.

## Project Structure

- **`main.cpp`**: The main application logic, including user registration, change detection, broadcasting, and CRDT-based merging.
- **`display.cpp` / `display.h`**: Handles the terminal UI, including displaying the document with color-coded changes and a summary of recent activity.
- **`DESIGNDOC.md`**: A detailed design document explaining the architecture and implementation details.
- **`README.md`**: This file.
- **`SyncText-A CRDT-Based Collaborative Text Editor.pdf`**: A research paper on CRDT-based collaborative text editors.
