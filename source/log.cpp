#include "log.h"

#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>

namespace {
std::string g_logPath;
}

void logInit(const std::string& path) {
    g_logPath = path;
    struct stat st;
    // keep the log small: start over when it grows past 256 KB
    if (stat(path.c_str(), &st) == 0 && st.st_size > 256 * 1024) std::remove(path.c_str());
}

void logf(const char* fmt, ...) {
    if (g_logPath.empty()) return;
    FILE* f = std::fopen(g_logPath.c_str(), "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}
