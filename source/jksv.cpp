#include "jksv.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fsutil.h"
#include "i18n.h"
#include "json.h"
#include "log.h"
#include "platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/miniz.h"
#pragma GCC diagnostic pop

namespace jksv {

const char* const kMetaName = ".nx_save_meta.bin";

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWithNoCase(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && lower(s.substr(s.size() - n)) == suffix;
}

int slotOf(const std::string& name) {
    // userN.dat (N = 1..99), case-insensitive
    std::string l = lower(name);
    if (l.size() < 9 || l.compare(0, 4, "user") != 0 || l.compare(l.size() - 4, 4, ".dat") != 0) return 0;
    std::string num = l.substr(4, l.size() - 8);
    if (num.empty() || num.size() > 2) return 0;
    for (char c : num)
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
    return std::atoi(num.c_str());
}

void sortEntries(std::vector<SaveEntry>& v) {
    std::sort(v.begin(), v.end(), [](const SaveEntry& a, const SaveEntry& b) {
        if ((a.slot > 0) != (b.slot > 0)) return a.slot > 0;
        if (a.slot != b.slot) return a.slot < b.slot;
        return lower(a.name) < lower(b.name);
    });
}

void walkDir(const std::string& root, const std::string& rel, int depth, std::vector<SaveEntry>& out) {
    if (depth > 8 || out.size() > 2000) return;
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(rel.empty() ? root : fsu::join(root, rel), entries)) return;
    for (const fsu::Entry& e : entries) {
        std::string relName = rel.empty() ? e.name : rel + "/" + e.name;
        if (e.isDir) {
            walkDir(root, relName, depth + 1, out);
        } else {
            if (rel.empty() && (e.name == kMetaName || e.name == ".sse_source")) continue;
            if (e.name.size() > 8 && e.name.compare(e.name.size() - 8, 8, ".sse_tmp") == 0) continue;
            SaveEntry se;
            se.name = relName;
            se.size = e.size;
            se.slot = rel.empty() ? slotOf(e.name) : 0;
            out.push_back(se);
        }
    }
}

struct ZipReader {
    mz_zip_archive zip;
    bool open = false;
    explicit ZipReader(const std::vector<uint8_t>& data) {
        std::memset(&zip, 0, sizeof(zip));
        open = !data.empty() && mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0);
    }
    ~ZipReader() {
        if (open) mz_zip_reader_end(&zip);
    }
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    int count() { return open ? static_cast<int>(mz_zip_reader_get_num_files(&zip)) : 0; }
    bool stat(int i, mz_zip_archive_file_stat& st) {
        return mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(i), &st) != 0;
    }
    int find(const std::string& name) {
        for (int i = 0; i < count(); ++i) {
            mz_zip_archive_file_stat st;
            if (stat(i, st) && !st.m_is_directory && name == st.m_filename) return i;
        }
        return -1;
    }
    bool extract(int i, std::vector<uint8_t>& out) {
        size_t size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(i), &size, 0);
        if (!p) return false;
        const uint8_t* b = static_cast<const uint8_t*>(p);
        out.assign(b, b + size);
        mz_free(p);
        return true;
    }
};

std::string titleHexOf(uint64_t tid) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(tid));
    return buf;
}

std::string zipError(mz_zip_archive& z) { return mz_zip_get_error_string(mz_zip_get_last_error(&z)); }

std::string zipOpenError(mz_zip_archive& z) {
    return tr("Nie można otworzyć archiwum ZIP: ", "Cannot open the ZIP archive: ") + zipError(z);
}

// Name that does not exist yet in dir: base, "base (2)", "base (3)", ... (+ suffix).
std::string uniqueName(const std::string& dir, const std::string& base, const std::string& suffix) {
    for (int n = 1; n < 100; ++n) {
        std::string name = base;
        if (n > 1) name += " (" + std::to_string(n) + ")";
        name += suffix;
        if (!fsu::exists(fsu::join(dir, name))) return name;
    }
    return base + " (x)" + suffix;
}

void writeMeta(const Backup& live, const std::string& dir) {
    const std::vector<uint8_t> meta = plat::liveMetaFile(live.titleId, live.userId);
    if (meta.empty()) {
        logf("no save meta for %s", live.userName.c_str());
        return;
    }
    std::string err;
    if (!fsu::writeVerified(fsu::join(dir, kMetaName), meta.data(), meta.size(), err)) logf("meta: %s", err.c_str());
}

} // namespace

std::string findWorkDir(std::string& configNote) {
    configNote.clear();
    std::string dir;
    std::vector<uint8_t> cfg;
    std::string err;
    if (fsu::readFile(plat::sdPath("sdmc:/config/JKSV/JKSV.json"), cfg, err)) {
        json::Value root;
        size_t end = 0;
        json::ParseError pe;
        if (json::parse(reinterpret_cast<const char*>(cfg.data()), cfg.size(), root, end, pe)) {
            const json::Value* wd = root.find("WorkingDirectory");
            if (wd && wd->type == json::Type::String && !wd->text.empty()) {
                dir = wd->text;
                configNote = tr("z JKSV.json", "from JKSV.json");
            }
        }
    }
    if (dir.empty()) dir = "sdmc:/JKSV";
    if (dir.compare(0, 5, "sdmc:") != 0) {
        if (dir[0] != '/') dir = "/" + dir;
        dir = "sdmc:" + dir;
    }
    while (dir.size() > 6 && dir.back() == '/') dir.pop_back();
    std::string mapped = plat::sdPath(dir);
    if (!fsu::isDir(mapped)) {
        std::string def = plat::sdPath("sdmc:/JKSV");
        if (fsu::isDir(def)) {
            mapped = def;
            configNote = tr("domyślny", "default");
        }
    }
    return mapped;
}

int countBackups(const std::string& dir) {
    int n = 0;
    std::vector<fsu::Entry> sub;
    if (fsu::listDir(dir, sub)) {
        for (const fsu::Entry& s : sub)
            if (!s.name.empty() && s.name[0] != '.' && (s.isDir || endsWithNoCase(s.name, ".zip"))) ++n;
    }
    return n;
}

bool listTitles(const std::string& workDir, std::vector<TitleDir>& out) {
    out.clear();
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(workDir, entries)) return false;
    for (const fsu::Entry& e : entries) {
        if (!e.isDir || e.name.empty() || e.name[0] == '.' || e.name == "_TRASH_") continue;
        TitleDir t;
        t.name = e.name;
        t.path = fsu::join(workDir, e.name);
        t.isSilksong = lower(e.name).find("silksong") != std::string::npos;
        t.backupCount = countBackups(t.path);
        out.push_back(t);
    }
    std::sort(out.begin(), out.end(), [](const TitleDir& a, const TitleDir& b) {
        if (a.isSilksong != b.isSilksong) return a.isSilksong;
        return lower(a.name) < lower(b.name);
    });
    return true;
}

bool listBackups(const std::string& titlePath, std::vector<Backup>& out, bool byName) {
    out.clear();
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(titlePath, entries)) return false;
    for (const fsu::Entry& e : entries) {
        if (e.name.empty() || e.name[0] == '.') continue;
        Backup b;
        b.name = e.name;
        b.path = fsu::join(titlePath, e.name);
        b.mtime = e.mtime;
        b.when = plat::formatTime(e.mtime);
        if (e.isDir) {
            b.isZip = false;
        } else if (endsWithNoCase(e.name, ".zip")) {
            b.isZip = true;
            b.size = e.size;
        } else {
            continue;
        }
        out.push_back(b);
    }
    std::sort(out.begin(), out.end(), [byName](const Backup& a, const Backup& b) {
        if (!byName && a.mtime != b.mtime) return a.mtime > b.mtime;
        return lower(a.name) > lower(b.name);
    });
    return true;
}

std::string normalizedName(const std::string& s) {
    std::string r;
    for (char c : lower(s))
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) r += c;
    return r;
}

Backup liveBackup(uint64_t titleId, const plat::SaveUser& user) {
    Backup b;
    b.isLive = true;
    b.titleId = titleId;
    b.userId = user.id;
    b.userName = user.name;
    b.name = user.name;
    return b;
}

bool listSaveFiles(const Backup& b, std::vector<SaveEntry>& out, std::string& err) {
    out.clear();
    if (b.isLive) {
        std::string root;
        if (!plat::liveMount(b.titleId, b.userId, true, root, err)) return false;
        walkDir(root, "", 0, out);
        plat::liveUnmount();
        sortEntries(out);
        return true;
    }
    if (!b.isZip) {
        if (!fsu::isDir(b.path)) {
            err = tr("Folder kopii nie istnieje: ", "The backup folder does not exist: ") + b.path;
            return false;
        }
        walkDir(b.path, "", 0, out);
        sortEntries(out);
        return true;
    }
    std::vector<uint8_t> data;
    if (!fsu::readFile(b.path, data, err)) return false;
    ZipReader z(data);
    if (!z.open) {
        err = zipOpenError(z.zip);
        return false;
    }
    for (int i = 0; i < z.count(); ++i) {
        mz_zip_archive_file_stat st;
        if (!z.stat(i, st) || st.m_is_directory) continue;
        std::string name = st.m_filename;
        if (name == kMetaName) continue;
        SaveEntry se;
        se.name = name;
        se.size = st.m_uncomp_size;
        se.slot = name.find('/') == std::string::npos ? slotOf(name) : 0;
        out.push_back(se);
    }
    sortEntries(out);
    return true;
}

bool readSaveFiles(const Backup& b, const std::vector<std::string>& entries, std::vector<std::vector<uint8_t>>& out,
                   std::string& err) {
    out.assign(entries.size(), std::vector<uint8_t>());
    if (b.isLive) {
        std::string root;
        if (!plat::liveMount(b.titleId, b.userId, true, root, err)) return false;
        for (size_t i = 0; i < entries.size(); ++i) {
            std::string e2;
            fsu::readFile(fsu::join(root, entries[i]), out[i], e2);
        }
        plat::liveUnmount();
        return true;
    }
    if (!b.isZip) {
        for (size_t i = 0; i < entries.size(); ++i) {
            std::string e2;
            fsu::readFile(fsu::join(b.path, entries[i]), out[i], e2);
        }
        return true;
    }
    std::vector<uint8_t> data;
    if (!fsu::readFile(b.path, data, err)) return false;
    ZipReader z(data);
    if (!z.open) {
        err = zipOpenError(z.zip);
        return false;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const int idx = z.find(entries[i]);
        if (idx >= 0 && !z.extract(idx, out[i])) out[i].clear();
    }
    return true;
}

bool readSaveFile(const Backup& b, const std::string& entry, std::vector<uint8_t>& out, std::string& err) {
    if (b.isLive) {
        std::string root;
        if (!plat::liveMount(b.titleId, b.userId, true, root, err)) return false;
        const bool ok = fsu::readFile(fsu::join(root, entry), out, err);
        plat::liveUnmount();
        return ok;
    }
    if (!b.isZip) return fsu::readFile(fsu::join(b.path, entry), out, err);
    std::vector<uint8_t> data;
    if (!fsu::readFile(b.path, data, err)) return false;
    ZipReader z(data);
    if (!z.open) {
        err = zipOpenError(z.zip);
        return false;
    }
    int idx = z.find(entry);
    if (idx < 0) {
        err = tr("W archiwum brak pliku ", "The archive has no file ") + entry;
        return false;
    }
    if (!z.extract(idx, out)) {
        err = tr("Nie można rozpakować ", "Cannot extract ") + entry + ": " + zipError(z.zip);
        return false;
    }
    return true;
}

bool rebuildZip(const std::vector<uint8_t>& srcZip, const std::string& entry, const std::vector<uint8_t>& data,
                std::vector<uint8_t>& outZip, std::string& err) {
    outZip.clear();
    ZipReader in(srcZip);
    if (!in.open) {
        err = zipOpenError(in.zip);
        return false;
    }
    mz_zip_archive out;
    std::memset(&out, 0, sizeof(out));
    if (!mz_zip_writer_init_heap(&out, 0, srcZip.size() + data.size() + 65536)) {
        err = tr("Nie można utworzyć archiwum ZIP.", "Cannot create the ZIP archive.");
        return false;
    }
    bool ok = true;
    bool replaced = false;
    const int n = in.count();
    for (int i = 0; i < n && ok; ++i) {
        mz_zip_archive_file_stat st;
        if (!in.stat(i, st)) {
            err = tr("Uszkodzony wpis w archiwum ZIP.", "Damaged entry in the ZIP archive.");
            ok = false;
            break;
        }
        if (!st.m_is_directory && entry == st.m_filename) {
            ok = mz_zip_writer_add_mem(&out, st.m_filename, data.data(), data.size(),
                                       static_cast<mz_uint>(MZ_DEFAULT_LEVEL)) != 0;
            replaced = true;
            if (!ok) err = tr("Nie można dodać pliku do ZIP: ", "Cannot add the file to the ZIP: ") + zipError(out);
        } else {
            ok = mz_zip_writer_add_from_zip_reader(&out, &in.zip, static_cast<mz_uint>(i)) != 0;
            if (!ok) err = tr("Nie można skopiować wpisu ZIP: ", "Cannot copy the ZIP entry: ") + std::string(st.m_filename);
        }
    }
    if (ok && !replaced) {
        err = tr("W archiwum brak pliku ", "The archive has no file ") + entry;
        ok = false;
    }
    void* buf = nullptr;
    size_t size = 0;
    if (ok) {
        ok = mz_zip_writer_finalize_heap_archive(&out, &buf, &size) != 0;
        if (!ok) err = tr("Nie można zakończyć archiwum ZIP: ", "Cannot finish the ZIP archive: ") + zipError(out);
    }
    if (ok && buf) {
        const uint8_t* b = static_cast<const uint8_t*>(buf);
        outZip.assign(b, b + size);
    }
    if (buf) mz_free(buf);
    mz_zip_writer_end(&out);
    if (!ok) return false;

    // Verify: archive is valid (CRC of every entry), same entry count, edited file matches.
    mz_zip_error verr = MZ_ZIP_NO_ERROR;
    if (!mz_zip_validate_mem_archive(outZip.data(), outZip.size(), 0, &verr)) {
        err = tr("Weryfikacja ZIP nie powiodła się: ", "ZIP verification failed: ") + std::string(mz_zip_get_error_string(verr));
        return false;
    }
    ZipReader check(outZip);
    std::vector<uint8_t> back;
    int idx = check.open ? check.find(entry) : -1;
    if (!check.open || check.count() != n || idx < 0 || !check.extract(idx, back) || back != data) {
        err = tr("Weryfikacja zawartości ZIP nie powiodła się.", "ZIP content verification failed.");
        return false;
    }
    return true;
}

std::string safeFileName(const std::string& s) {
    std::string r;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || std::strchr("<>:\"/\\|?*", c)) r += '_';
        else r += c;
    }
    while (!r.empty() && (r.back() == ' ' || r.back() == '.')) r.pop_back();
    if (r.empty()) r = "_";
    return r;
}

std::string makeEditedName(const Backup& src, const std::string& stamp) {
    std::string base = src.name;
    if (src.isZip && endsWithNoCase(base, ".zip")) base = base.substr(0, base.size() - 4);
    size_t cut = base.find(" - EDIT ");
    if (cut != std::string::npos) base = base.substr(0, cut);
    return uniqueName(fsu::parentDir(src.path), base + " - EDIT " + stamp, src.isZip ? ".zip" : "");
}

std::string makeLiveCopyName(const std::string& titleDir, const Backup& live, const std::string& stamp) {
    return uniqueName(titleDir, safeFileName(live.userName) + " - EDIT " + stamp, "");
}

std::string makeSafetyName(const std::string& dir, const std::string& stamp, const std::string& name,
                           const std::string& suffix) {
    return uniqueName(dir, stamp + " - " + safeFileName(name), suffix);
}

bool saveAsNewBackup(const Backup& src, const std::string& entry, const std::vector<uint8_t>& data,
                     const std::string& newName, Backup& created, std::string& err) {
    const std::string dst = fsu::join(fsu::parentDir(src.path), newName);
    if (fsu::exists(dst)) {
        err = tr("Kopia o tej nazwie już istnieje: ", "A backup with this name already exists: ") + newName;
        return false;
    }
    created = src;
    created.name = newName;
    created.path = dst;
    if (src.isZip) {
        std::vector<uint8_t> zipData, outZip;
        if (!fsu::readFile(src.path, zipData, err)) return false;
        if (!rebuildZip(zipData, entry, data, outZip, err)) return false;
        if (!fsu::writeFile(dst, outZip.data(), outZip.size(), err)) return false;
        std::vector<uint8_t> back;
        if (!fsu::readFile(dst, back, err) || back != outZip) {
            std::remove(dst.c_str());
            if (err.empty()) err = tr("Weryfikacja zapisu nie powiodła się: ", "Write verification failed: ") + dst;
            return false;
        }
        created.size = outZip.size();
        logf("saved new zip backup %s (%u bytes)", dst.c_str(), static_cast<unsigned>(outZip.size()));
        return true;
    }
    if (!fsu::copyDir(src.path, dst, err)) {
        fsu::removeTree(dst);
        return false;
    }
    if (!fsu::replaceFile(fsu::join(dst, entry), data.data(), data.size(), err)) {
        fsu::removeTree(dst);
        return false;
    }
    logf("saved new folder backup %s", dst.c_str());
    return true;
}

bool saveLiveAsNewBackup(const Backup& live, const std::string& entry, const std::vector<uint8_t>& data,
                         const std::string& titleDir, const std::string& newName, Backup& created, std::string& err) {
    const std::string dst = fsu::join(titleDir, newName);
    if (fsu::exists(dst)) {
        err = tr("Kopia o tej nazwie już istnieje: ", "A backup with this name already exists: ") + newName;
        return false;
    }
    std::string root;
    if (!plat::liveMount(live.titleId, live.userId, true, root, err)) return false;
    const bool copied = fsu::copyDir(root, dst, err);
    plat::liveUnmount();
    if (!copied) {
        fsu::removeTree(dst);
        return false;
    }
    writeMeta(live, dst);
    const std::string target = fsu::join(dst, entry);
    fsu::mkdirs(fsu::parentDir(target));
    if (!fsu::replaceFile(target, data.data(), data.size(), err)) {
        fsu::removeTree(dst);
        return false;
    }
    created = Backup();
    created.name = newName;
    created.path = dst;
    logf("saved game save of %s as backup %s", live.userName.c_str(), dst.c_str());
    return true;
}

bool overwriteBackup(const Backup& b, const std::string& entry, const std::vector<uint8_t>& data,
                     const std::string& safetyDir, std::string& safetyCopy, std::string& err) {
    if (!fsu::mkdirs(safetyDir)) {
        err = tr("Nie można utworzyć folderu kopii bezpieczeństwa: ", "Cannot create the safety copy folder: ") + safetyDir;
        return false;
    }
    const bool zipName = b.isZip && endsWithNoCase(b.name, ".zip");
    safetyCopy = fsu::join(safetyDir, makeSafetyName(safetyDir, plat::localTimeStamp(),
                                                     zipName ? b.name.substr(0, b.name.size() - 4) : b.name,
                                                     zipName ? ".zip" : ""));
    bool copied = b.isZip ? fsu::copyFile(b.path, safetyCopy, err) : fsu::copyDir(b.path, safetyCopy, err);
    if (!copied) {
        err = tr("Nie udało się zrobić kopii bezpieczeństwa: ", "Could not create the safety copy: ") + err;
        return false;
    }
    if (b.isZip) {
        std::vector<uint8_t> zipData, outZip;
        if (!fsu::readFile(b.path, zipData, err)) return false;
        if (!rebuildZip(zipData, entry, data, outZip, err)) return false;
        if (!fsu::replaceFile(b.path, outZip.data(), outZip.size(), err)) return false;
    } else {
        if (!fsu::replaceFile(fsu::join(b.path, entry), data.data(), data.size(), err)) return false;
    }
    logf("overwrote backup %s (safety copy: %s)", b.path.c_str(), safetyCopy.c_str());
    return true;
}

bool writeLive(const Backup& live, const std::string& entry, const std::vector<uint8_t>& data,
               const std::string& safetyDir, std::string& safetyCopy, std::string& err) {
    if (!live.isLive) {
        err = "internal: not a game save";
        return false;
    }
    if (live.titleId && plat::runningGame() == live.titleId) {
        err = tr("Ta gra jest uruchomiona w tle. Zamknij ją (HOME, potem X) i spróbuj ponownie – inaczej gra "
                 "nadpisałaby zmiany.",
                 "This game is running in the background. Close it (HOME, then X) and try again - otherwise "
                 "the game would overwrite the changes.");
        return false;
    }
    const auto unchanged = [&err] {
        err += tr("\nZapis gry nie został zmieniony.", "\nThe game save was not changed.");
        return false;
    };
    if (!fsu::mkdirs(safetyDir)) {
        err = tr("Nie można utworzyć folderu kopii bezpieczeństwa: ", "Cannot create the safety copy folder: ") + safetyDir;
        return unchanged();
    }
    safetyCopy = fsu::join(safetyDir, makeSafetyName(safetyDir, plat::localTimeStamp(),
                                                     live.userName + " - " + tr("zapis gry", "game save")));
    std::string root;
    if (!plat::liveMount(live.titleId, live.userId, false, root, err)) {
        safetyCopy.clear();
        return unchanged();
    }
    // 1. a full copy of the current game save, restorable with JKSV or this editor
    if (!fsu::copyDir(root, safetyCopy, err)) {
        plat::liveUnmount();
        fsu::removeTree(safetyCopy);
        safetyCopy.clear();
        err = tr("Nie udało się zrobić kopii bezpieczeństwa: ", "Could not create the safety copy: ") + err;
        return unchanged();
    }
    writeMeta(live, safetyCopy);
    {
        // which game and user the copy belongs to (lets the editor write it back later)
        const std::string src = "{\"title\":\"" + titleHexOf(live.titleId) + "\",\"user\":" +
                                json::escapeString(live.userName) + "}\n";
        std::string e2;
        fsu::writeVerified(fsu::join(safetyCopy, ".sse_source"), src.data(), src.size(), e2);
    }
    // 2. write and verify inside the journaled save data - nothing is permanent before the commit
    const std::string target = fsu::join(root, entry);
    if (entry.find('/') != std::string::npos) fsu::mkdirs(fsu::parentDir(target));
    if (!fsu::writeVerified(target, data.data(), data.size(), err)) {
        plat::liveUnmount();
        return unchanged();
    }
    // 3. commit
    if (!plat::liveCommit(err)) {
        plat::liveUnmount();
        return unchanged();
    }
    plat::liveUnmount();
    // 4. read it back from a fresh mount
    std::vector<uint8_t> back;
    std::string e2;
    if (!readSaveFile(live, entry, back, e2) || back != data) {
        err = tr("Zapis w grze nie przeszedł weryfikacji. ", "The game save failed verification. ") + e2;
        return false;
    }
    logf("wrote %s into the game save of %s (%u bytes, safety copy: %s)", entry.c_str(), live.userName.c_str(),
         static_cast<unsigned>(data.size()), safetyCopy.c_str());
    return true;
}

int pruneBackups(const std::string& dir, int keep) {
    if (keep <= 0) return 0;
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(dir, entries)) return 0;
    std::vector<std::string> names;
    for (const fsu::Entry& e : entries)
        if (!e.name.empty() && e.name[0] != '.') names.push_back(e.name);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return a > b; });
    int removed = 0;
    for (size_t i = static_cast<size_t>(keep); i < names.size(); ++i) {
        if (fsu::removeTree(fsu::join(dir, names[i]))) ++removed;
        else logf("prune: cannot remove %s", names[i].c_str());
    }
    if (removed) logf("pruned %d old safety copies in %s", removed, dir.c_str());
    return removed;
}

bool removeBackup(const Backup& b) {
    if (b.isLive || b.path.empty()) return false;
    const bool ok = fsu::removeTree(b.path);
    logf("removed backup %s: %d", b.path.c_str(), ok ? 1 : 0);
    return ok;
}

} // namespace jksv
