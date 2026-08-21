#ifndef SYNCTEXT_REGISTRY_H
#define SYNCTEXT_REGISTRY_H

#include "types.h"
#include <string>
#include <vector>

// Shared-memory user discovery.
bool attach_registry(Editor &e);
void detach_registry(Editor &e);
int register_user(Editor &e);      // returns slot, or -1 on failure
void unregister_user(Editor &e);
std::vector<std::string> active_users(const Editor &e);
void print_active(const Editor &e);

#endif
