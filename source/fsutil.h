// Small stdio/POSIX file helpers that work on the Switch SD card (sdmc:/) and on a PC.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fsu {

struct Entry {
    std::string name;
    bool isDir = false;
    uint64_t size = 0;
    int64_t mtime = 0;
};

std::string join(const std::string& dir, const std::string& name);
std::string fileName(const std::string& path);
std::string parentDir(const std::string& path);

bool exists(const std::string& path);
bool isDir(const std::string& path);
bool isFile(const std::string& path);

bool listDir(const std::string& path, std::vector<Entry>& out);
bool mkdirs(const std::string& path);

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string& err);
bool writeFile(const std::string& path, const void* data, size_t size, std::string& err);
// Compares the file on disk with data.
bool verifyFile(const std::string& path, const void* data, size_t size, std::string& err);
// Writes the file in place, then reads it back to verify it.
bool writeVerified(const std::string& path, const void* data, size_t size, std::string& err);
// Writes to "<path>.sse_tmp", verifies it, then replaces <path>.
bool replaceFile(const std::string& path, const void* data, size_t size, std::string& err);

bool copyFile(const std::string& src, const std::string& dst, std::string& err);
bool copyDir(const std::string& src, const std::string& dst, std::string& err);
bool removeTree(const std::string& path);

} // namespace fsu
