#include "fsutil.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "i18n.h"

namespace fsu {

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

std::string fileName(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string parentDir(const std::string& path) {
    size_t p = path.find_last_of('/');
    if (p == std::string::npos) return "";
    if (p > 0 && path[p - 1] == ':') return path.substr(0, p + 1);  // "sdmc:/"
    return path.substr(0, p);
}

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool listDir(const std::string& path, std::vector<Entry>& out) {
    out.clear();
    DIR* d = opendir(path.c_str());
    if (!d) return false;
    while (struct dirent* e = readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        Entry en;
        en.name = e->d_name;
        struct stat st;
        if (stat(join(path, en.name).c_str(), &st) == 0) {
            en.isDir = S_ISDIR(st.st_mode);
            en.size = en.isDir ? 0 : static_cast<uint64_t>(st.st_size);
            en.mtime = static_cast<int64_t>(st.st_mtime);
        } else {
#ifdef DT_DIR
            en.isDir = e->d_type == DT_DIR;
#endif
        }
        out.push_back(en);
    }
    closedir(d);
    return true;
}

bool mkdirs(const std::string& path) {
    if (path.empty()) return false;
    if (isDir(path)) return true;
    // skip a device prefix like "sdmc:/"
    size_t pos = 0;
    size_t colon = path.find(":/");
    if (colon != std::string::npos) pos = colon + 2;
    else if (path[0] == '/') pos = 1;
    while (true) {
        size_t slash = path.find('/', pos);
        std::string part = slash == std::string::npos ? path : path.substr(0, slash);
        if (!part.empty() && part.back() != ':' && !isDir(part)) {
            if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST) return false;
        }
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return isDir(path);
}

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = tr("Nie można otworzyć pliku: ", "Cannot open file: ") + path;
        return false;
    }
    std::vector<uint8_t> chunk(256 * 1024);
    while (true) {
        size_t n = std::fread(chunk.data(), 1, chunk.size(), f);
        if (n) out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n));
        if (n < chunk.size()) {
            if (std::ferror(f)) {
                std::fclose(f);
                err = tr("Błąd odczytu pliku: ", "Read error: ") + path;
                return false;
            }
            break;
        }
    }
    std::fclose(f);
    return true;
}

bool writeFile(const std::string& path, const void* data, size_t size, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        err = tr("Nie można utworzyć pliku: ", "Cannot create file: ") + path;
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        size_t n = std::fwrite(p + done, 1, size - done < 256 * 1024 ? size - done : 256 * 1024, f);
        if (n == 0) {
            std::fclose(f);
            std::remove(path.c_str());
            err = tr("Błąd zapisu (brak miejsca?): ", "Write error (out of space?): ") + path;
            return false;
        }
        done += n;
    }
    if (std::fclose(f) != 0) {
        std::remove(path.c_str());
        err = tr("Błąd zamykania pliku: ", "Error closing file: ") + path;
        return false;
    }
    return true;
}

bool verifyFile(const std::string& path, const void* data, size_t size, std::string& err) {
    std::vector<uint8_t> back;
    if (!readFile(path, back, err)) return false;
    if (back.size() != size || (size && std::memcmp(back.data(), data, size) != 0)) {
        err = tr("Weryfikacja zapisu nie powiodła się: ", "Write verification failed: ") + path;
        return false;
    }
    return true;
}

bool writeVerified(const std::string& path, const void* data, size_t size, std::string& err) {
    return writeFile(path, data, size, err) && verifyFile(path, data, size, err);
}

bool replaceFile(const std::string& path, const void* data, size_t size, std::string& err) {
    const std::string tmp = path + ".sse_tmp";
    std::remove(tmp.c_str());
    if (!writeFile(tmp, data, size, err)) return false;
    if (!verifyFile(tmp, data, size, err)) {
        std::remove(tmp.c_str());
        return false;
    }
    if (exists(path) && std::remove(path.c_str()) != 0) {
        std::remove(tmp.c_str());
        err = tr("Nie można zastąpić pliku: ", "Cannot replace file: ") + path;
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = tr("Nie można zmienić nazwy pliku tymczasowego: ", "Cannot rename temporary file: ") + tmp;
        return false;
    }
    return verifyFile(path, data, size, err);
}

bool copyFile(const std::string& src, const std::string& dst, std::string& err) {
    std::vector<uint8_t> data;
    if (!readFile(src, data, err)) return false;
    if (!writeFile(dst, data.data(), data.size(), err)) return false;
    return verifyFile(dst, data.data(), data.size(), err);
}

bool copyDir(const std::string& src, const std::string& dst, std::string& err) {
    if (!mkdirs(dst)) {
        err = tr("Nie można utworzyć folderu: ", "Cannot create folder: ") + dst;
        return false;
    }
    std::vector<Entry> entries;
    if (!listDir(src, entries)) {
        err = tr("Nie można odczytać folderu: ", "Cannot read folder: ") + src;
        return false;
    }
    for (const Entry& e : entries) {
        const std::string s = join(src, e.name);
        const std::string d = join(dst, e.name);
        if (e.isDir) {
            if (!copyDir(s, d, err)) return false;
        } else {
            if (!copyFile(s, d, err)) return false;
        }
    }
    return true;
}

bool removeTree(const std::string& path) {
    if (isDir(path)) {
        std::vector<Entry> entries;
        if (listDir(path, entries)) {
            for (const Entry& e : entries) removeTree(join(path, e.name));
        }
        return rmdir(path.c_str()) == 0;
    }
    return std::remove(path.c_str()) == 0;
}

} // namespace fsu
