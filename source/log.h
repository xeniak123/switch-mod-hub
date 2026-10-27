// Tiny append-only log in sdmc:/switch/SwitchModHub/log.txt (useful for bug reports).
#pragma once

#include <string>

void logInit(const std::string& path);
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
