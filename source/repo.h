// The online mod index (mods/index.json in the GitHub repository) and matching it with games.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace repo {

extern const char* const kRepo;      // "xeniak123/switch-mod-hub"
extern const char* const kIndexUrl;  // raw URL of mods/index.json

struct RemoteMod {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string game;       // display name of the game
    std::string gameMatch;  // without a title id: part of the game name, e.g. "Silksong"
    std::string type;       // romfs / exefs / cheats / mixed
    std::string url;        // https download of the mod archive (.zip)
    std::string sha256;     // of the archive
    std::string gameVersion;
    uint64_t titleId = 0;  // 0 = match by gameMatch
    uint64_t size = 0;
    std::vector<std::string> tags;
};

struct Index {
    int format = 0;
    std::string updated;
    std::vector<RemoteMod> mods;
    int skipped = 0;  // entries with missing or invalid fields
};

bool parseIndex(const std::vector<uint8_t>& data, Index& out, std::string& err);
// Like strcmp for dotted versions ("1.10.0" > "1.9.2"); a leading 'v' is ignored.
int compareVersions(const std::string& a, const std::string& b);
bool matchesGame(const RemoteMod& m, uint64_t titleId, const std::string& gameName);
// Case-insensitive search in the name, game, author, tags and description.
bool matchesQuery(const RemoteMod& m, const std::string& query);

} // namespace repo
