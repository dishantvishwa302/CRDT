// Message queues: each user has one inbox. Broadcast = send to every other inbox.
// Incoming messages are pushed onto a mutex-protected queue for the monitor thread.

#include "ipc.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>

using namespace std;

static string escape_field(const string &s) {
    string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '|') out += "<P>";
        else if (c == '\n') out += "<N>";
        else out += c;
    }
    return out;
}

static string unescape_field(string s) {
    size_t p;
    while ((p = s.find("<P>")) != string::npos) s.replace(p, 3, "|");
    while ((p = s.find("<N>")) != string::npos) s.replace(p, 3, "\n");
    return s;
}

string serialize(const Update &u) {
    ostringstream ss;
    ss << u.op << '|' << u.line << '|' << u.col_start << '|' << u.col_end << '|'
       << u.timestamp << '|' << escape_field(u.user_id) << '|'
       << escape_field(u.old_text) << '|' << escape_field(u.new_text);
    return ss.str();
}

bool deserialize(const string &s, Update &u) {
    vector<string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find('|', start);
        if (pos == string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    if (parts.size() < 8) return false;
    try {
        u.op = parts[0];
        u.line = stoi(parts[1]);
        u.col_start = stoi(parts[2]);
        u.col_end = stoi(parts[3]);
        u.timestamp = stoll(parts[4]);
        u.user_id = unescape_field(parts[5]);
        u.old_text = unescape_field(parts[6]);
        u.new_text = unescape_field(parts[7]);
    } catch (...) {
        return false;
    }
    return true;
}

bool open_inbox(Editor &e) {
    struct mq_attr attr {};
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSG_SIZE;
    e.mq = mq_open(e.mq_name.c_str(), O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr);
    if (e.mq == (mqd_t)-1) {
        perror("mq_open");
        return false;
    }
    return true;
}

void close_inbox(Editor &e) {
    if (e.mq != (mqd_t)-1) {
        mq_close(e.mq);
        mq_unlink(e.mq_name.c_str());
        e.mq = (mqd_t)-1;
    }
}

void send_to_all(Editor &e, const Update &u) {
    if (!e.registry) return;
    string msg = serialize(u);
    if (msg.size() >= (size_t)MQ_MSG_SIZE) {
        cerr << "Update too large to send (line " << u.line << ")\n";
        return;
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (!e.registry->slot_in_use[i]) continue;
        if (e.user_id == e.registry->user_id[i]) continue;

        mqd_t q = mq_open(e.registry->mq_name[i], O_WRONLY | O_NONBLOCK);
        if (q == (mqd_t)-1) continue;
        mq_send(q, msg.c_str(), msg.size(), 0);
        mq_close(q);
    }
}

void listener(Editor &e) {
    char buf[MQ_MSG_SIZE + 1];
    while (e.running) {
        ssize_t n = mq_receive(e.mq, buf, MQ_MSG_SIZE, nullptr);
        if (n >= 0) {
            buf[n] = '\0';
            Update u;
            if (deserialize(buf, u)) {
                lock_guard<mutex> lock(e.incoming_mu);
                e.incoming.push(u);
            }
        } else {
            this_thread::sleep_for(chrono::milliseconds(200));
        }
    }
}

vector<Update> take_incoming(Editor &e) {
    vector<Update> out;
    lock_guard<mutex> lock(e.incoming_mu);
    while (!e.incoming.empty()) {
        out.push_back(e.incoming.front());
        e.incoming.pop();
    }
    return out;
}
