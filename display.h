#ifndef SYNCTEXT_DISPLAY_H
#define SYNCTEXT_DISPLAY_H

#include <string>
#include <vector>

enum LineStatus {
    LINE_NORMAL,
    LINE_MODIFIED,
    LINE_NEW,
    LINE_DELETED
};

void clearScreen();
void showDocument(const std::vector<std::string> &doc,
                  const std::string &title,
                  const std::vector<LineStatus> &statuses = {});
void showSummary(const std::vector<std::string> &lines);
void restoreCursor();

#endif
