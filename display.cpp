// display.cpp
// Implementation of terminal color display functions.

#include "display.h"
using namespace std;

// ANSI color codes
static const char* RESET = "\033[0m";
static const char* BOLD = "\033[1m";
static const char* DIM = "\033[2m";
static const char* FG_RED = "\033[31m";
static const char* FG_GREEN = "\033[32m";
static const char* FG_YELLOW = "\033[33m";
static const char* FG_CYAN = "\033[36m";
static const char* FG_MAGENTA = "\033[35m";
static const char* FG_BLUE = "\033[34m";
static const char* FG_WHITE = "\033[37m";

static std::mutex print_mutex;

void display_init() {
    // Could add terminal setup here later if needed.
}

void clearScreen() {
    // Save cursor position, clear screen, move cursor to top-left, hide cursor
    // cout << "\033[H\033[J\033[?25l" << flush;
}


void printColoredLine(int lineNo, const string &text, LineStatus status) {
    lock_guard<mutex> lg(print_mutex);
    const char* color = FG_WHITE;
    string tag;
    switch (status) {
        case LINE_NEW: color = FG_GREEN; tag = "[NEW] "; break;
        case LINE_MODIFIED: color = FG_YELLOW; tag = "[MOD] "; break;
        case LINE_DELETED: color = FG_RED; tag = "[DEL] "; break;
        default: tag = "      "; color = FG_WHITE; break;
    }
    cout << BOLD << FG_CYAN << "Line " << lineNo << ":" << RESET << " "
         << color << tag << RESET << text << "\n";
}

void showDocument(const vector<string> &doc, const string &title, const vector<LineStatus> &statuses) {
    lock_guard<mutex> lg(print_mutex);
    clearScreen();

    using namespace chrono;
    auto now = system_clock::now();
    time_t t = system_clock::to_time_t(now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&t));

    cout << BOLD << FG_MAGENTA << "Document: " << title << RESET << "  "
         << DIM << "(" << timebuf << ")" << RESET << "\n";
    cout << string(60, '-') << "\n";

    for (size_t i = 0; i < doc.size(); ++i) {
        LineStatus st = LINE_NORMAL;
        if (i < statuses.size()) st = statuses[i];
        const char* color = FG_WHITE;
        string tag;
        switch (st) {
            case LINE_NEW: color = FG_GREEN; tag = "[NEW] "; break;
            case LINE_MODIFIED: color = FG_YELLOW; tag = "[MOD] "; break;
            case LINE_DELETED: color = FG_RED; tag = "[DEL] "; break;
            default: color = FG_WHITE; tag = ""; break;
        }
        cout << BOLD << FG_CYAN << "Line " << i << ":" << RESET << " "
             << color << tag << RESET << doc[i] << "\n";
    }

    cout << string(60, '-') << "\n";
}

void showSummary(const vector<string> &lines) {
    lock_guard<mutex> lg(print_mutex);
    cout << BOLD << FG_BLUE << "Recent activity:" << RESET << "\n";
    if (lines.empty()) {
        cout << DIM << "(none)" << RESET << "\n";
    } else {
        for (auto &l : lines) {
            cout << " - " << l << "\n";
        }
    }
    cout << "\n";
}

void safePrintLn(const string &line) {
    lock_guard<mutex> lg(print_mutex);
    cout << line << "\n";
}

void restoreCursor() {
    cout << "\033[?25h" << flush;
}
