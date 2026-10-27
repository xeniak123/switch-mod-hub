#include "modengine.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "fsutil.h"
#include "i18n.h"
#include "json.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/miniz.h"
#pragma GCC diagnostic pop

namespace mods {
namespace {

const char* const kKinds[] = {"romfs", "exefs", "cheats", "exefs_patches"};

struct FileRec {
    std::string path;  // relative to the SD root
    uint64_t size = 0;
    uint32_t crc = 0;
    bool backup = false;  // a file that existed before was moved to the backup folder
};

struct Enabled {
    uint64_t titleId = 0;
    std::string id;
    std::vector<FileRec> files;
};

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string statePath(const Paths& p) { return fsu::join(p.modsDir, "state.json"); }
std::string gameDir(const Paths& p, uint64_t tid) { return fsu::join(p.modsDir, titleHex(tid)); }
std::string modDir(const Paths& p, uint64_t tid, const std::string& id) { return fsu::join(gameDir(p, tid), id); }
std::string backupRoot(const Paths& p, uint64_t tid, const std::string& id) {
    return fsu::join(fsu::join(fsu::join(p.modsDir, ".backup"), titleHex(tid)), id);
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64 || id[0] == '.') return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}

// ---- state.json ----

std::vector<Enabled> loadState(const Paths& p) {
    std::vector<Enabled> st;
    std::vector<uint8_t> data;
    std::string err;
    if (!fsu::readFile(statePath(p), data, err)) return st;
    json::Value root;
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(reinterpret_cast<const char*>(data.data()), data.size(), root, end, pe)) return st;
    const json::Value* list = root.find("enabled");
    if (!list || list->type != json::Type::Array) return st;
    for (const json::Value& e : list->items) {
        Enabled rec;
        const json::Value* t = e.find("title");
        const json::Value* id = e.find("id");
        const json::Value* files = e.find("files");
        if (!t || !id || !files || t->type != json::Type::String || id->type != json::Type::String ||
            !parseTitleHex(t->text, rec.titleId))
            continue;
        rec.id = id->text;
        for (const json::Value& f : files->items) {
            FileRec r;
            const json::Value* path = f.find("path");
            const json::Value* size = f.find("size");
            const json::Value* crc = f.find("crc");
            const json::Value* bk = f.find("backup");
            if (!path || path->type != json::Type::String) continue;
            r.path = path->text;
            if (size && json::isIntegerNumber(*size)) r.size = static_cast<uint64_t>(json::asInt(*size));
            if (crc && json::isIntegerNumber(*crc)) r.crc = static_cast<uint32_t>(json::asInt(*crc));
            r.backup = bk && bk->type == json::Type::Bool && bk->boolean;
            rec.files.push_back(r);
        }
        st.push_back(rec);
    }
    return st;
}

bool saveState(const Paths& p, const std::vector<Enabled>& st, std::string& err) {
    std::string s = "{\"format\":1,\"enabled\":[";
    for (size_t i = 0; i < st.size(); ++i) {
        if (i) s += ",";
        s += "{\"title\":\"" + titleHex(st[i].titleId) + "\",\"id\":" + json::escapeString(st[i].id) + ",\"files\":[";
        for (size_t k = 0; k < st[i].files.size(); ++k) {
            const FileRec& f = st[i].files[k];
            if (k) s += ",";
            s += "{\"path\":" + json::escapeString(f.path) + ",\"size\":" + std::to_string(f.size) +
                 ",\"crc\":" + std::to_string(f.crc) + ",\"backup\":" + (f.backup ? "true" : "false") + "}";
        }
        s += "]}";
    }
    s += "]}\n";
    fsu::mkdirs(p.modsDir);
    return fsu::replaceFile(statePath(p), s.data(), s.size(), err);
}

const Enabled* findEnabled(const std::vector<Enabled>& st, uint64_t tid, const std::string& id) {
    for (const Enabled& e : st)
        if (e.titleId == tid && e.id == id) return &e;
    return nullptr;
}

// ---- mod.json ----

void readMeta(const std::string& dir, Mod& m) {
    std::vector<uint8_t> data;
    std::string err;
    if (!fsu::readFile(fsu::join(dir, "mod.json"), data, err)) return;
    json::Value root;
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(reinterpret_cast<const char*>(data.data()), data.size(), root, end, pe)) return;
    auto str = [&root](const char* key, std::string& out) {
        const json::Value* v = root.find(key);
        if (v && v->type == json::Type::String && !v->text.empty()) out = v->text;
    };
    str("name", m.name);
    str("version", m.version);
    str("author", m.author);
    str("description", m.description);
    str("source", m.source);
    if (const json::Value* t = root.find("tags")) {
        m.tags.clear();
        for (const json::Value& v : t->items)
            if (v.type == json::Type::String) m.tags.push_back(v.text);
    }
}

bool writeMeta(const std::string& dir, const Mod& m, std::string& err) {
    std::string s = "{\"id\":" + json::escapeString(m.id) + ",\"name\":" + json::escapeString(m.name) +
                    ",\"version\":" + json::escapeString(m.version) + ",\"author\":" + json::escapeString(m.author) +
                    ",\"description\":" + json::escapeString(m.description) + ",\"source\":" +
                    json::escapeString(m.source.empty() ? "local" : m.source) + ",\"tags\":[";
    for (size_t i = 0; i < m.tags.size(); ++i) s += (i ? "," : "") + json::escapeString(m.tags[i]);
    s += "]}\n";
    return fsu::writeVerified(fsu::join(dir, "mod.json"), s.data(), s.size(), err);
}

// ---- files ----

void collect(const std::string& base, const std::string& rel, int depth, std::vector<std::string>& out) {
    if (depth > 24 || out.size() > 100000) return;
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(fsu::join(base, rel), entries)) return;
    for (const fsu::Entry& e : entries) {
        const std::string r = rel + "/" + e.name;
        if (e.isDir) collect(base, r, depth + 1, out);
        else out.push_back(r);
    }
}

// Files of a mod relative to its folder ("romfs/data/a.bin"), sorted.
std::vector<std::string> modFiles(const Paths& p, uint64_t tid, const std::string& id) {
    std::vector<std::string> out;
    const std::string dir = modDir(p, tid, id);
    for (const char* k : kKinds)
        if (fsu::isDir(fsu::join(dir, k))) collect(dir, k, 0, out);
    std::sort(out.begin(), out.end());
    return out;
}

// Where a mod file goes, relative to the SD root.
std::string targetFor(uint64_t tid, const std::string& rel) {
    if (rel.compare(0, 14, "exefs_patches/") == 0) return "atmosphere/" + rel;
    return "atmosphere/contents/" + titleHex(tid) + "/" + rel;
}

bool fileCrc(const std::string& path, uint32_t& crc, uint64_t& size) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> buf(64 * 1024);
    mz_ulong c = MZ_CRC32_INIT;
    size = 0;
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        c = mz_crc32(c, buf.data(), n);
        size += n;
    }
    const bool ok = !std::ferror(f);
    std::fclose(f);
    crc = static_cast<uint32_t>(c);
    return ok;
}

// Streaming copy (works with little memory, e.g. in the overlay), verified by re-reading.
bool copyStream(const std::string& src, const std::string& dst, uint32_t& crc, uint64_t& size, std::string& err) {
    fsu::mkdirs(fsu::parentDir(dst));
    FILE* in = std::fopen(src.c_str(), "rb");
    if (!in) {
        err = tr("Nie można otworzyć pliku: ", "Cannot open file: ") + src;
        return false;
    }
    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        err = tr("Nie można utworzyć pliku: ", "Cannot create file: ") + dst;
        return false;
    }
    std::vector<uint8_t> buf(64 * 1024);
    mz_ulong c = MZ_CRC32_INIT;
    size = 0;
    bool ok = true;
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), in)) > 0) {
        c = mz_crc32(c, buf.data(), n);
        size += n;
        if (std::fwrite(buf.data(), 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    ok = ok && !std::ferror(in);
    std::fclose(in);
    ok = (std::fclose(out) == 0) && ok;
    crc = static_cast<uint32_t>(c);
    uint32_t c2 = 0;
    uint64_t s2 = 0;
    if (!ok || !fileCrc(dst, c2, s2) || c2 != crc || s2 != size) {
        std::remove(dst.c_str());
        err = tr("Błąd zapisu (brak miejsca?): ", "Write error (out of space?): ") + dst;
        return false;
    }
    return true;
}

bool moveFile(const std::string& src, const std::string& dst) {
    fsu::mkdirs(fsu::parentDir(dst));
    std::remove(dst.c_str());
    if (std::rename(src.c_str(), dst.c_str()) == 0) return true;
    uint32_t crc = 0;
    uint64_t size = 0;
    std::string err;
    if (!copyStream(src, dst, crc, size, err)) return false;
    std::remove(src.c_str());
    return true;
}

bool dirEmpty(const std::string& dir) {
    std::vector<fsu::Entry> e;
    return fsu::listDir(dir, e) && e.empty();
}

// Removes the empty folders above a file, up to atmosphere/contents/<title id> (inclusive).
void removeEmptyParents(const std::string& sdRoot, const std::string& relFile) {
    std::string rel = fsu::parentDir(relFile);
    while (!rel.empty() && rel != "atmosphere" && rel != "atmosphere/contents" && rel != "atmosphere/exefs_patches") {
        const std::string full = sdRoot + rel;
        if (!dirEmpty(full) || rmdir(full.c_str()) != 0) break;
        rel = fsu::parentDir(rel);
    }
}

void removeIfEmpty(const std::string& dir) {
    if (fsu::isDir(dir) && dirEmpty(dir)) rmdir(dir.c_str());
}

// Puts everything back after a failed enable.
void rollback(const Paths& p, const Enabled& rec) {
    for (auto it = rec.files.rbegin(); it != rec.files.rend(); ++it) {
        const std::string full = p.sdRoot + it->path;
        std::remove(full.c_str());
        if (it->backup) moveFile(fsu::join(backupRoot(p, rec.titleId, rec.id), it->path), full);
        removeEmptyParents(p.sdRoot, it->path);
    }
}

// ---- archives ----

std::vector<std::string> splitPath(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == '/' || c == '\\') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(cur);
    return parts;
}

// Path inside the mod folder for an archive entry ("romfs/..."), "" when it is not a mod file.
std::string mapEntry(const std::vector<std::string>& parts, uint64_t tid, bool& otherGame) {
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string c = lower(parts[i]);
        uint64_t t = 0;
        if (c == "contents" && i + 1 < parts.size() && parseTitleHex(parts[i + 1], t) && t != tid) otherGame = true;
        size_t need = 0;
        std::string kind;
        if (c == "romfs" || c == "exefs" || c == "cheats") {
            need = 1;
            kind = c;
        } else if (c == "exefs_patches") {
            need = 2;
            kind = c;
        } else {
            continue;
        }
        if (i + need >= parts.size()) return "";
        std::string out = kind;
        for (size_t k = i + 1; k < parts.size(); ++k) out += "/" + parts[k];
        return out;
    }
    return "";
}

bool unsafePath(const std::vector<std::string>& parts) {
    if (parts.empty() || parts[0].empty()) return true;  // absolute path
    for (const std::string& s : parts)
        if (s == ".." || s.find(':') != std::string::npos) return true;
    return false;
}

size_t writeToFile(void* opaque, mz_uint64, const void* buf, size_t n) {
    return std::fwrite(buf, 1, n, static_cast<FILE*>(opaque));
}

} // namespace

std::string titleHex(uint64_t titleId) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(titleId));
    return buf;
}

bool parseTitleHex(const std::string& s, uint64_t& titleId) {
    if (s.size() != 16) return false;
    uint64_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | static_cast<uint64_t>(d);
    }
    titleId = v;
    return true;
}

std::string safeId(const std::string& s) {
    std::string r;
    for (char c : lower(s)) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_') r += c;
        else if (!r.empty() && r.back() != '-') r += '-';
    }
    while (!r.empty() && (r.back() == '-' || r.back() == '.')) r.pop_back();
    while (!r.empty() && r[0] == '.') r.erase(0, 1);
    if (r.size() > 48) r.resize(48);
    return r.empty() ? "mod" : r;
}

bool isEnabled(const Paths& p, uint64_t titleId, const std::string& id) {
    return findEnabled(loadState(p), titleId, id) != nullptr;
}

std::vector<Mod> list(const Paths& p, uint64_t titleId) {
    std::vector<Mod> out;
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(gameDir(p, titleId), entries)) return out;
    const std::vector<Enabled> st = loadState(p);
    for (const fsu::Entry& e : entries) {
        if (!e.isDir || e.name.empty() || e.name[0] == '.') continue;
        Mod m;
        m.titleId = titleId;
        m.id = e.name;
        m.name = e.name;
        readMeta(modDir(p, titleId, e.name), m);
        for (const std::string& f : modFiles(p, titleId, e.name)) {
            ++m.files;
            struct stat sb;
            if (stat(fsu::join(modDir(p, titleId, e.name), f).c_str(), &sb) == 0) m.bytes += static_cast<uint64_t>(sb.st_size);
            m.romfs = m.romfs || f.compare(0, 6, "romfs/") == 0;
            m.exefs = m.exefs || f.compare(0, 6, "exefs/") == 0;
            m.cheats = m.cheats || f.compare(0, 7, "cheats/") == 0;
            m.patches = m.patches || f.compare(0, 14, "exefs_patches/") == 0;
        }
        m.enabled = findEnabled(st, titleId, e.name) != nullptr;
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const Mod& a, const Mod& b) { return lower(a.name) < lower(b.name); });
    return out;
}

std::vector<uint64_t> gamesWithMods(const Paths& p) {
    std::vector<uint64_t> out;
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(p.modsDir, entries)) return out;
    for (const fsu::Entry& e : entries) {
        uint64_t tid = 0;
        if (e.isDir && parseTitleHex(e.name, tid) && !list(p, tid).empty()) out.push_back(tid);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool installZip(const Paths& p, uint64_t titleId, const std::string& id, const std::vector<uint8_t>& zip,
                const Mod& meta, std::string& err) {
    if (!validId(id)) {
        err = tr("Nieprawidłowy identyfikator moda: ", "Invalid mod id: ") + id;
        return false;
    }
    mz_zip_archive z;
    std::memset(&z, 0, sizeof(z));
    if (zip.empty() || !mz_zip_reader_init_mem(&z, zip.data(), zip.size(), 0)) {
        err = tr("To nie jest poprawne archiwum ZIP.", "This is not a valid ZIP archive.");
        return false;
    }
    std::vector<std::pair<mz_uint, std::string>> files;
    bool otherGame = false, unsafe = false;
    int metaIndex = -1;
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory) continue;
        const std::vector<std::string> parts = splitPath(st.m_filename);
        if (unsafePath(parts)) {
            unsafe = true;
            continue;
        }
        const std::string mapped = mapEntry(parts, titleId, otherGame);
        if (!mapped.empty()) files.push_back({i, mapped});
        else if (lower(parts.back()) == "mod.json" && metaIndex < 0 && parts.size() <= 2) metaIndex = static_cast<int>(i);
    }
    if (unsafe || otherGame || files.empty()) {
        mz_zip_reader_end(&z);
        if (unsafe) err = tr("Archiwum zawiera niebezpieczne ścieżki – odrzucono.", "The archive contains unsafe paths - rejected.");
        else if (otherGame) err = tr("Ten mod jest dla innej gry.", "This mod is for a different game.");
        else err = tr("Archiwum nie zawiera plików moda (romfs, exefs, cheats ani exefs_patches).",
                      "The archive contains no mod files (romfs, exefs, cheats or exefs_patches).");
        return false;
    }
    const std::string tmp = fsu::join(gameDir(p, titleId), ".new-" + id);
    fsu::removeTree(tmp);
    bool ok = fsu::mkdirs(tmp);
    for (size_t k = 0; ok && k < files.size(); ++k) {
        const std::string dst = fsu::join(tmp, files[k].second);
        fsu::mkdirs(fsu::parentDir(dst));
        FILE* f = std::fopen(dst.c_str(), "wb");
        if (!f) {
            ok = false;
            break;
        }
        ok = mz_zip_reader_extract_to_callback(&z, files[k].first, writeToFile, f, 0) != 0;
        ok = (std::fclose(f) == 0) && ok;
    }
    Mod m = meta;
    m.id = id;
    if (ok && metaIndex >= 0) {
        // the archive's own mod.json fills what the caller did not know
        const std::string metaTmp = fsu::join(tmp, ".archive");
        fsu::mkdirs(metaTmp);
        FILE* f = std::fopen(fsu::join(metaTmp, "mod.json").c_str(), "wb");
        if (f) {
            mz_zip_reader_extract_to_callback(&z, static_cast<mz_uint>(metaIndex), writeToFile, f, 0);
            std::fclose(f);
            Mod a;
            readMeta(metaTmp, a);
            if (m.name.empty()) m.name = a.name;
            if (m.version.empty()) m.version = a.version;
            if (m.author.empty()) m.author = a.author;
            if (m.description.empty()) m.description = a.description;
            if (m.tags.empty()) m.tags = a.tags;
        }
        fsu::removeTree(metaTmp);
    }
    mz_zip_reader_end(&z);
    if (m.name.empty()) m.name = id;
    if (ok) ok = writeMeta(tmp, m, err);
    if (!ok) {
        fsu::removeTree(tmp);
        if (err.empty()) err = tr("Nie udało się rozpakować moda (brak miejsca na karcie SD?).", "Could not unpack the mod (SD card full?).");
        return false;
    }
    const bool wasEnabled = isEnabled(p, titleId, id);
    if (wasEnabled && !disable(p, titleId, id, err)) {
        fsu::removeTree(tmp);
        return false;
    }
    const std::string dst = modDir(p, titleId, id);
    fsu::removeTree(dst);
    if (std::rename(tmp.c_str(), dst.c_str()) != 0) {
        std::string e2;
        if (!fsu::copyDir(tmp, dst, e2)) {
            fsu::removeTree(tmp);
            fsu::removeTree(dst);
            err = e2;
            return false;
        }
        fsu::removeTree(tmp);
    }
    if (wasEnabled) {
        std::string e2;
        if (!enable(p, titleId, id, e2)) {
            err = tr("Mod zaktualizowano, ale nie udało się go włączyć: ", "The mod was updated but could not be enabled: ") + e2;
            return false;
        }
    }
    return true;
}

bool removeMod(const Paths& p, uint64_t titleId, const std::string& id, std::string& err) {
    if (!validId(id)) {
        err = tr("Nieprawidłowy identyfikator moda: ", "Invalid mod id: ") + id;
        return false;
    }
    if (isEnabled(p, titleId, id) && !disable(p, titleId, id, err)) return false;
    fsu::removeTree(modDir(p, titleId, id));
    fsu::removeTree(backupRoot(p, titleId, id));
    removeIfEmpty(gameDir(p, titleId));
    return !fsu::exists(modDir(p, titleId, id));
}

bool enable(const Paths& p, uint64_t titleId, const std::string& id, std::string& err, std::vector<std::string>* conflicts) {
    std::vector<Enabled> st = loadState(p);
    if (findEnabled(st, titleId, id)) return true;
    const std::vector<std::string> files = modFiles(p, titleId, id);
    if (files.empty()) {
        err = tr("Mod nie zawiera plików do zainstalowania.", "The mod has no files to install.");
        return false;
    }
    // every file can belong to one enabled mod only
    std::map<std::string, std::string> owner;
    for (const Enabled& e : st) {
        Mod m;
        m.name = e.id;
        readMeta(modDir(p, e.titleId, e.id), m);
        for (const FileRec& f : e.files) owner[lower(f.path)] = m.name;
    }
    std::vector<std::string> clash;
    for (const std::string& rel : files) {
        auto it = owner.find(lower(targetFor(titleId, rel)));
        if (it != owner.end() && std::find(clash.begin(), clash.end(), it->second) == clash.end()) clash.push_back(it->second);
    }
    if (!clash.empty()) {
        if (conflicts) *conflicts = clash;
        err = tr("Konflikt: te same pliki zmienia włączony mod ", "Conflict: an enabled mod changes the same files: ");
        for (size_t i = 0; i < clash.size(); ++i) err += (i ? ", " : "") + clash[i];
        err += tr(". Najpierw go wyłącz.", ". Disable it first.");
        return false;
    }
    Enabled rec;
    rec.titleId = titleId;
    rec.id = id;
    const std::string src = modDir(p, titleId, id);
    for (const std::string& rel : files) {
        FileRec f;
        f.path = targetFor(titleId, rel);
        const std::string full = p.sdRoot + f.path;
        if (fsu::exists(full)) {
            // a file the user (or the game's own LayeredFS setup) already had: keep it for later
            if (!moveFile(full, fsu::join(backupRoot(p, titleId, id), f.path))) {
                rollback(p, rec);
                err = tr("Nie można zabezpieczyć istniejącego pliku: ", "Cannot back up the existing file: ") + f.path;
                return false;
            }
            f.backup = true;
        }
        rec.files.push_back(f);
        if (!copyStream(fsu::join(src, rel), full, rec.files.back().crc, rec.files.back().size, err)) {
            rollback(p, rec);
            return false;
        }
    }
    st.push_back(rec);
    if (!saveState(p, st, err)) {
        rollback(p, rec);
        return false;
    }
    return true;
}

bool disable(const Paths& p, uint64_t titleId, const std::string& id, std::string& err, int* skipped) {
    std::vector<Enabled> st = loadState(p);
    size_t idx = st.size();
    for (size_t i = 0; i < st.size(); ++i)
        if (st[i].titleId == titleId && st[i].id == id) idx = i;
    if (skipped) *skipped = 0;
    if (idx == st.size()) return true;
    int skip = 0;
    for (const FileRec& f : st[idx].files) {
        const std::string full = p.sdRoot + f.path;
        uint32_t crc = 0;
        uint64_t size = 0;
        const bool present = fsu::exists(full);
        if (present && fileCrc(full, crc, size) && crc == f.crc && size == f.size) {
            std::remove(full.c_str());
        } else if (present) {
            ++skip;  // changed after enabling: leave it (and its backup) alone
            continue;
        }
        if (f.backup) moveFile(fsu::join(backupRoot(p, titleId, id), f.path), full);
        removeEmptyParents(p.sdRoot, f.path);
    }
    st.erase(st.begin() + static_cast<std::ptrdiff_t>(idx));
    if (!saveState(p, st, err)) return false;
    if (skip == 0) fsu::removeTree(backupRoot(p, titleId, id));
    if (skipped) *skipped = skip;
    return true;
}

} // namespace mods
