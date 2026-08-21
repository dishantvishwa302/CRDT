// Terminal view: document + a short activity log. Color = what changed.

#include "display.h"

#include <iostream>
#include <mutex>
#include <chrono>
#include <ctime>

using namespace std;

static const char *RESET = "\033[0m";
static const char *BOLD = "\033[1m";
static const char *DIM = "\033[2m";
static const char *FG_RED = "\033[31m";
static const char *FG_GREEN = "\033[32m";
static const char *FG_YELLOW = "\033[33m";
static const char *FG_CYAN = "\033[36m";
static const char *FG_MAGENTA = "\033[35m";
static const char *FG_BLUE = "\033[34m";
static const char *FG_WHITE = "\033[37m";

static mutex print_mu;

void clearScreen() {
    cout << "\033[2J\033[H" << flush;
}

void restoreCursor() {
    cout << "\033[?25h" << flush;
}

static void line_style(LineStatus st, const char *&color, string &tag) {
    switch (st) {
        case LINE_NEW:      color = FG_GREEN;  tag = "[NEW] "; break;
        case LINE_MODIFIED: color = FG_YELLOW; tag = "[MOD] "; break;
        case LINE_DELETED:  color = FG_RED;    tag = "[DEL] "; break;
        default:            color = FG_WHITE;  tag = "";       break;
    }
}

void showDocument(const vector<string> &doc, const string &title,
                  const vector<LineStatus> &statuses) {
    lock_guard<mutex> lock(print_mu);
    clearScreen();

    time_t t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&t));

    cout << BOLD << FG_MAGENTA << "Document: " << title << RESET << "  "
         << DIM << "(" << timebuf << ")" << RESET << "\n";
    cout << string(60, '-') << "\n";

    for (size_t i = 0; i < doc.size(); i++) {
        LineStatus st = (i < statuses.size()) ? statuses[i] : LINE_NORMAL;
        const char *color = FG_WHITE;
        string tag;
        line_style(st, color, tag);
        cout << BOLD << FG_CYAN << "Line " << i << ":" << RESET << " "
             << color << tag << RESET << doc[i] << "\n";
    }
    cout << string(60, '-') << "\n";
}

void showSummary(const vector<string> &lines) {
    lock_guard<mutex> lock(print_mu);
    cout << BOLD << FG_BLUE << "Recent activity:" << RESET << "\n";
    if (lines.empty()) {
        cout << DIM << "(none)" << RESET << "\n";
    } else {
        for (const auto &line : lines) cout << " - " << line << "\n";
    }
    cout << "\n";
}
