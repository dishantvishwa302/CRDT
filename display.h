// display.h
// Simple terminal UI helpers with color formatting.
// No dependencies other than iostream/vector/string.

#ifndef DISPLAY_H
#define DISPLAY_H

#include <string>
#include <vector>
#include <mutex>
#include <iostream>
#include <chrono>
#include <ctime>

using namespace std;

// --- Line status indicators for color display ---
enum LineStatus {
    LINE_NORMAL,
    LINE_MODIFIED,
    LINE_NEW,
    LINE_DELETED
};

// --- Forward declarations ---
void display_init();
void clearScreen();
void printColoredLine(int lineNo, const string &text, LineStatus status);
void showDocument(const vector<string> &doc, const string &title, const vector<LineStatus> &statuses = {});
void showSummary(const vector<string> &lines);
void safePrintLn(const string &line);
void restoreCursor();


#endif // DISPLAY_H