// Mod engine: a per-game library of mods on the SD card and their installation into Atmosphère's
// LayeredFS folders (atmosphere/contents/<title id>/romfs, exefs, cheats and atmosphere/exefs_patches).
// Every file a mod puts on the SD card is recorded in state.json, so disabling a mod removes exactly
// its files and puts back the files it replaced. Shared by the app and the overlay.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mods {

struct Paths {
    std::string sdRoot;   // real path of "sdmc:/" (ends with '/')
    std::string modsDir;  // the library, e.g. sdmc:/switch/SwitchModHub/mods
};

struct Mod {
    uint64_t titleId = 0;
    std::string id;  // folder name in the library
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string source;  // "repo" or "local"
    std::vector<std::string> tags;
    bool enabled = false;
    int files = 0;  // files the mod installs
    uint64_t bytes = 0;
    bool romfs = false, exefs = false, cheats = false, patches = false;
};

std::string titleHex(uint64_t titleId);  // "0100ABCD12340000"
bool parseTitleHex(const std::string& s, uint64_t& titleId);
// Folder-safe id ("My Mod v2" -> "my-mod-v2").
std::string safeId(const std::string& s);

// Mods of one game, sorted by name.
std::vector<Mod> list(const Paths& p, uint64_t titleId);
// Games that have at least one mod in the library.
std::vector<uint64_t> gamesWithMods(const Paths& p);
bool isEnabled(const Paths& p, uint64_t titleId, const std::string& id);

// Unpacks a mod archive into the library as `id`. Accepted layouts: romfs/ exefs/ cheats/ exefs_patches/
// at the top or inside sub-folders, and SD card layouts (atmosphere/contents/<title id>/...).
// An enabled older version is disabled first and enabled again afterwards.
bool installZip(const Paths& p, uint64_t titleId, const std::string& id, const std::vector<uint8_t>& zip,
                const Mod& meta, std::string& err);
// Disables the mod (if needed) and deletes it from the library.
bool removeMod(const Paths& p, uint64_t titleId, const std::string& id, std::string& err);
// Copies the mod's files to the SD card. Refuses when another enabled mod owns one of the files
// (`conflicts` receives the names of those mods); nothing is changed then.
bool enable(const Paths& p, uint64_t titleId, const std::string& id, std::string& err,
            std::vector<std::string>* conflicts = nullptr);
// Removes the mod's files and restores replaced ones. Files changed since enabling are left alone
// and counted in `skipped`.
bool disable(const Paths& p, uint64_t titleId, const std::string& id, std::string& err, int* skipped = nullptr);

} // namespace mods
