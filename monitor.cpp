// Watch the local file with stat(). On change, diff lines, stamp them, send immediately.
// On remote updates, run LWW and write the winner back to the file.

#include "monitor.h"
#include "crdt.h"
#include "ipc.h"
#include "display.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <sys/stat.h>

using namespace std;

vector<string> read_lines(const string &path) {
    vector<string> lines;
    ifstream in(path);
    string line;
    while (getline(in, line)) lines.push_back(line);
    return lines;
}

void write_lines(const string &path, const vector<string> &lines) {
    ofstream out(path);
    for (const auto &line : lines) out << line << "\n";
}

void init_doc(const string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return;
    ofstream out(path);
    out << "Hello World\n"
        << "This is a collaborative editor\n"
        << "Welcome to SyncText\n"
        << "Edit this file and watch the other terminals update\n";
}

// Longest common prefix + suffix. Returns [start, end] of the changed span in `old_line`.
static pair<int, int> diff_columns(const string &old_line, const string &new_line) {
    int n = (int)old_line.size();
    int m = (int)new_line.size();
    int i = 0;
    while (i < n && i < m && old_line[i] == new_line[i]) i++;
    if (i == n && i == m) return {0, -1};

    int j = 0;
    while (j < (n - i) && j < (m - i) && old_line[n - 1 - j] == new_line[m - 1 - j]) j++;

    int start = i;
    int end_old = n - 1 - j;
    if (end_old < start) end_old = start - 1;
    return {start, end_old};
}

static Update make_update(const Editor &e, int line,
                          const string &old_line, const string &new_line) {
    auto cols = diff_columns(old_line, new_line);
    Update u;
    u.line = line;
    u.col_start = cols.first;
    u.col_end = cols.second;
    u.old_text = old_line;
    u.new_text = new_line;
    u.timestamp = now_ms();
    u.user_id = e.user_id;
    if (old_line.empty() && !new_line.empty()) u.op = "insert";
    else if (!old_line.empty() && new_line.empty()) u.op = "delete";
    else u.op = "replace";
    return u;
}

static vector<LineStatus> statuses_from_diff(const vector<string> &before,
                                             const vector<string> &after) {
    vector<LineStatus> st(after.size(), LINE_NORMAL);
    int n = max((int)before.size(), (int)after.size());
    for (int i = 0; i < n; i++) {
        string a = (i < (int)before.size()) ? before[i] : "";
        string b = (i < (int)after.size()) ? after[i] : "";
        if (a == b) continue;
        if (i >= (int)after.size()) continue;
        if (a.empty() && !b.empty()) st[i] = LINE_NEW;
        else if (!a.empty() && b.empty()) st[i] = LINE_DELETED;
        else st[i] = LINE_MODIFIED;
    }
    return st;
}

static bool sleep_while_running(Editor &e, int ms) {
    const int step = 100;
    for (int waited = 0; waited < ms && e.running; waited += step)
        this_thread::sleep_for(chrono::milliseconds(step));
    return e.running;
}

void monitor_loop(Editor &e) {
    e.lines = read_lines(e.doc_path);
    e.meta.assign(e.lines.size(), LineMeta{});

    struct stat sb {};
    stat(e.doc_path.c_str(), &sb);
    time_t mtime = sb.st_mtime;
    off_t size = sb.st_size;

    showDocument(e.lines, e.doc_path, {});
    showSummary({"Watching " + e.doc_path + " — edit it in another editor."});

    while (sleep_while_running(e, 1000)) {
        if (stat(e.doc_path.c_str(), &sb) == -1) continue;

        // ----- local file change -----
        if (sb.st_mtime != mtime || sb.st_size != size) {
            mtime = sb.st_mtime;
            size = sb.st_size;

            auto now = read_lines(e.doc_path);
            vector<string> before = e.lines;
            int n = max((int)before.size(), (int)now.size());
            vector<string> summary;

            for (int i = 0; i < n; i++) {
                string old_line = (i < (int)before.size()) ? before[i] : "";
                string new_line = (i < (int)now.size()) ? now[i] : "";
                if (old_line == new_line) continue;

                Update u = make_update(e, i, old_line, new_line);
                apply_update(e, u);   // stamp LWW metadata for this line
                send_to_all(e, u);    // tell everyone immediately
                summary.push_back(e.user_id + " sent line " + to_string(i));
            }

            e.lines = now;
            e.meta.resize(e.lines.size());

            showDocument(e.lines, e.doc_path, statuses_from_diff(before, now));
            if (summary.empty()) summary.push_back("File changed (no line diffs).");
            showSummary(summary);
        }

        // ----- remote updates -----
        auto remote = take_incoming(e);
        if (remote.empty()) continue;

        vector<string> before = e.lines;
        int applied = apply_all(e, remote);
        if (applied == 0) continue;

        write_lines(e.doc_path, e.lines);
        if (stat(e.doc_path.c_str(), &sb) == 0) {
            mtime = sb.st_mtime;
            size = sb.st_size;
        }

        vector<string> summary;
        for (const auto &u : remote)
            summary.push_back(u.user_id + " updated line " + to_string(u.line));

        showDocument(e.lines, e.doc_path, statuses_from_diff(before, e.lines));
        showSummary(summary);
    }
}
