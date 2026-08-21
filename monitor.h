#ifndef SYNCTEXT_MONITOR_H
#define SYNCTEXT_MONITOR_H

#include "types.h"
#include <string>
#include <vector>

std::vector<std::string> read_lines(const std::string &path);
void write_lines(const std::string &path, const std::vector<std::string> &lines);
void init_doc(const std::string &path);
void monitor_loop(Editor &e);

#endif
