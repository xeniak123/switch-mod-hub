// Access to JKSV backups on the SD card and to the game's own save data on the console.
// JKSV stores backups in <WorkingDirectory>/<Game title>/ either as folders or as .zip files,
// each containing the save files plus ".nx_save_meta.bin" (which must be preserved for restore).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform.h"

namespace jksv {

extern const char* const kMetaName;  // ".nx_save_meta.bin"

struct TitleDir {
    std::string name;
    std::string path;       // JKSV folder ("" when the game has none)
    uint64_t titleId = 0;   // 0 = unknown (a JKSV folder that matches no installed game)
    bool isSilksong = false;
    bool isSafety = false;  // the editor's own safety copies
    bool exists = true;     // false: no JKSV folder, only the game save / mods are listed
    int backupCount = 0;
    int liveCount = 0;      // console users with save data of this game
    int modsInstalled = 0;
    int modsEnabled = 0;
    int icon = -1;          // index into the app's icon cache
};

struct Backup {
    std::string name;  // file / folder name (game save: user nickname)
    std::string path;
    bool isZip = false;
    bool isLive = false;   // the game's own save data on the console (no JKSV involved)
    uint64_t titleId = 0;  // game save: the game
    std::string userId;    // game save: console user id
    std::string userName;  // game save: nickname
    uint64_t size = 0;
    int64_t mtime = 0;
    std::string when;  // formatted local mtime ("" if unknown)
};

struct SaveEntry {
    std::string name;  // path inside the backup ("user1.dat")
    uint64_t size = 0;
    int slot = 0;      // 1..4 for userN.dat, 0 otherwise
};

// Working directory from sdmc:/config/JKSV/JKSV.json (falls back to sdmc:/JKSV).
std::string findWorkDir(std::string& configNote);

bool listTitles(const std::string& workDir, std::vector<TitleDir>& out);
// Newest first: by modification time, or by name (descending) when byName is set.
bool listBackups(const std::string& titlePath, std::vector<Backup>& out, bool byName = false);
int countBackups(const std::string& dir);

// The game's own save of a console user, as a list entry.
Backup liveBackup(uint64_t titleId, const plat::SaveUser& user);
// Lower-case letters and digits only - to match JKSV folder names with game names.
std::string normalizedName(const std::string& s);

bool listSaveFiles(const Backup& b, std::vector<SaveEntry>& out, std::string& err);
bool readSaveFile(const Backup& b, const std::string& entry, std::vector<uint8_t>& out, std::string& err);
// Reads several entries with one zip read / one mount. Entries that cannot be read stay empty.
bool readSaveFiles(const Backup& b, const std::vector<std::string>& entries, std::vector<std::vector<uint8_t>>& out,
                   std::string& err);

// Creates a copy of backup `src` next to it (name `newName`) with `entry` replaced by `data`.
bool saveAsNewBackup(const Backup& src, const std::string& entry, const std::vector<uint8_t>& data,
                     const std::string& newName, Backup& created, std::string& err);

// Copies the whole game save of `live` into titleDir/newName (JKSV folder backup) with `entry` replaced.
bool saveLiveAsNewBackup(const Backup& live, const std::string& entry, const std::vector<uint8_t>& data,
                         const std::string& titleDir, const std::string& newName, Backup& created, std::string& err);

// Replaces `entry` inside backup `b`. A full copy of the original backup is stored in
// `safetyDir` first; its path is returned in `safetyCopy`.
bool overwriteBackup(const Backup& b, const std::string& entry, const std::vector<uint8_t>& data,
                     const std::string& safetyDir, std::string& safetyCopy, std::string& err);

// Writes `entry` into the game save of `live`. A full copy of the current game save (JKSV folder
// format with .nx_save_meta.bin) is stored in `safetyDir` first; its path is returned in `safetyCopy`.
// The game save only changes when every step (write, verification, commit) succeeds.
bool writeLive(const Backup& live, const std::string& entry, const std::vector<uint8_t>& data,
               const std::string& safetyDir, std::string& safetyCopy, std::string& err);

// "<name> - EDIT <stamp>[.zip]" that does not exist yet in the title folder.
std::string makeEditedName(const Backup& src, const std::string& stamp);
// "<nickname> - EDIT <stamp>" that does not exist yet in titleDir.
std::string makeLiveCopyName(const std::string& titleDir, const Backup& live, const std::string& stamp);
// "<stamp> - <name><suffix>" that does not exist yet in dir.
std::string makeSafetyName(const std::string& dir, const std::string& stamp, const std::string& name,
                           const std::string& suffix = "");
// Replaces characters that are not allowed in FAT file names.
std::string safeFileName(const std::string& s);

// Deletes the oldest entries of dir so that `keep` remain (0 = keep all). The entries are ordered
// by name, newest first (safety copies start with a timestamp). Returns the number removed.
int pruneBackups(const std::string& dir, int keep);
bool removeBackup(const Backup& b);

// Builds a new zip from `srcZip` with `entry` replaced. Exposed for tests.
bool rebuildZip(const std::vector<uint8_t>& srcZip, const std::string& entry, const std::vector<uint8_t>& data,
                std::vector<uint8_t>& outZip, std::string& err);

} // namespace jksv
