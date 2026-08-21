#ifndef SYNCTEXT_IPC_H
#define SYNCTEXT_IPC_H

#include "types.h"
#include <string>
#include <vector>

// POSIX message queues + Update serialization.
bool open_inbox(Editor &e);
void close_inbox(Editor &e);
void send_to_all(Editor &e, const Update &u);
void listener(Editor &e);
std::vector<Update> take_incoming(Editor &e);

std::string serialize(const Update &u);
bool deserialize(const std::string &s, Update &u);

#endif
