// Self update from the GitHub releases of the repository.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace update {

extern const char* const kLatestUrl;  // GitHub API: latest release
extern const char* const kAssetName;  // "SwitchModHub.nro"

struct Release {
    std::string tag;      // "v2.1.0"
    std::string name;
    std::string notes;
    std::string url;      // download of the .nro asset
    std::string sha256;   // from the asset digest ("" if GitHub did not provide one)
    uint64_t size = 0;
};

bool parseLatest(const std::vector<uint8_t>& json, Release& out, std::string& err);
bool isNewer(const std::string& remoteTag, const std::string& currentVersion);
// Homebrew executable header check ("NRO0" at offset 0x10).
bool looksLikeNro(const std::vector<uint8_t>& data);
// Verifies `data` (size, NRO header, SHA-256 if known) and replaces the .nro at selfPath.
// The old file is kept as <selfPath>.old until the swap has succeeded.
bool install(const std::string& selfPath, const std::vector<uint8_t>& data, const Release& rel, std::string& err);

} // namespace update
