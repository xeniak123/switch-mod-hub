// Persistent user settings (sdmc:/switch/SwitchModHub/settings.json).
#pragma once

#include <string>

#include "i18n.h"

namespace settings {

struct Settings {
    i18n::Lang language = i18n::Lang::Polish;
    int keepBackups = 20;  // safety copies kept in the editor's backup folder (0 = keep all)
    bool autoUpdate = true;  // check GitHub for a new version at start
};

Settings& get();
// A missing or invalid file leaves the defaults.
void load(const std::string& path);
bool save(const std::string& path);
// Next value of the "keep N safety copies" option: 10 -> 20 -> 50 -> 0 (all) -> 10.
int nextKeepValue(int current);

} // namespace settings
