// Layout, colours, paths and small helpers shared by the app's source files (app*.cpp).
#pragma once

#include <cstdint>
#include <string>

#include "gfx.h"
#include "json.h"

namespace ui {

const int kW = 1280;
const int kH = 720;
const int kHeaderH = 80;
const int kFooterY = 668;
const int kListX = 32;
const int kListW = 820;
const int kInfoX = 868;
const int kInfoW = 380;
const int kBrowseY = 96;
const int kBrowseH = 556;
const int kBrowseTabsH = 48;  // tab bar on top of the browser list (home / game screens)
const int kEditY = 142;
const int kEditH = 510;
const int kBrowseRowH = 66;
const int kQuickRowH = 44;
const int kTreeRowH = 44;
const int kTreeListTop = 52;
const int kActionRowH = 66;
const int kChangeRowH = 56;
const int kModalItemH = 60;
const int kHexRowH = 30;

const char* const kAppDirSd = "sdmc:/switch/SwitchModHub";
const char* const kSafetyDirSd = "sdmc:/switch/SwitchModHub/backups";
const char* const kLegacySafetyDirSd = "sdmc:/switch/SilksongSaveEditor/backups";
const char* const kImportDirSd = "sdmc:/switch/SwitchModHub/import";
const char* const kModImportDirSd = "sdmc:/switch/SwitchModHub/import-mods";
const char* const kExportDirSd = "sdmc:/switch/SwitchModHub/export";
const char* const kSettingsSd = "sdmc:/switch/SwitchModHub/settings.json";
const char* const kLegacySettingsSd = "sdmc:/switch/SilksongSaveEditor/settings.json";
const char* const kDefinitionsSd = "sdmc:/switch/SwitchModHub/definitions.json";
const char* const kModsDirSd = "sdmc:/switch/SwitchModHub/mods";
const char* const kCacheDirSd = "sdmc:/switch/SwitchModHub/cache";

namespace col {
const uint32_t bgTop = hexColor(0x1C1017);
const uint32_t bgBottom = hexColor(0x09060A);
const uint32_t bar = hexColor(0x120A0E, 215);
const uint32_t panel = hexColor(0x1D1319, 238);
const uint32_t panel2 = hexColor(0x2A1D25);
const uint32_t panelBorder = hexColor(0x3D2A35);
const uint32_t accent = hexColor(0xD8434F);
const uint32_t accentDim = hexColor(0x8F2632);
const uint32_t rowSel = hexColor(0xD8434F, 46);
const uint32_t gold = hexColor(0xE8C893);
const uint32_t text = hexColor(0xF5EDE7);
const uint32_t dim = hexColor(0xBCAAB3);
const uint32_t muted = hexColor(0x84727D);
const uint32_t dark = hexColor(0x1A1015);
const uint32_t ok = hexColor(0x6FD08F);
const uint32_t warn = hexColor(0xF2B64F);
const uint32_t err = hexColor(0xF26A6A);
const uint32_t toggleOff = hexColor(0x4A3742);
const uint32_t joyBlue = hexColor(0x3FA7E0);
} // namespace col

std::string formatSize(uint64_t b);
std::string lowerAscii(const std::string& s);
bool containsNoCase(const std::string& hay, const std::string& needleLower);
// Shows SD paths as "sdmc:/..." even when the platform maps them elsewhere (PC tests).
std::string displaySd(const std::string& realPath);
std::string elementsText(size_t n);
std::string fieldsText(size_t n);
std::string typeName(const json::Value& v);
std::string valuePreview(const json::Value& v);
std::string formatDuration(double seconds);
std::string formatHours(double seconds);
bool parseIntText(std::string s, long long& out);
bool parseFloatText(std::string s, double& out);
long long addClamped(long long a, long long b);
long long clampLL(long long v, long long lo, long long hi);
// Whether an integer field should be kept in 32-bit range (Unity "int" fields).
bool keepInt32(const json::Value& v);
void revertTree(json::Value& v);
std::string elementName(const json::Value& e);
uint32_t buttonFor(const std::string& badge);
std::string saveName(const json::Value& v);

} // namespace ui
