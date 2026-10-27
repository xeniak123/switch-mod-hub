// Silksong-specific knowledge: curated "quick edit" rows, in-game names and bulk actions.
// Everything is looked up in the loaded save, so fields that do not exist are simply hidden.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "json.h"

namespace silk {

enum class RowKind { Header, Int, Float, Bool, Choice };

struct QuickRow {
    RowKind kind = RowKind::Header;
    std::string label;   // human readable
    std::string key;     // path / internal name shown in small text, e.g. "playerData.geo"
    std::string help;    // longer description for the side panel
    json::Value* value = nullptr;  // edited value (Choice: the main field, shown in the details)
    std::vector<json::Value*> linked;                        // Int: copied from value on change
    std::vector<std::pair<json::Value*, bool>> whenOn;       // Bool: also set these when turned on
    std::vector<std::pair<json::Value*, bool>> whenChanged;  // Bool: also set these on any change
    std::vector<std::pair<json::Value*, bool>> whenPositive; // Int: also set these while the value is > 0
    long long minValue = 0;
    long long maxValue = 2147483647LL;
    long long gameMax = 0;  // Int: highest value the game itself uses (0 = unknown); more gets a warning
    bool isTime = false;    // seconds, shown as h:mm:ss
    std::vector<std::string> choices;    // Choice: option labels
    std::function<int()> getChoice;      // Choice: current option (-1 = unusual combination)
    std::function<void(int)> setChoice;  // Choice: applies an option
    std::vector<json::Value*> touched;   // Choice: every value the row may change
    std::vector<std::string> touchedNames;  // Choice: field names of `touched` (for the details panel)
};

// Builds the quick-edit rows. Pointers stay valid until the document structure changes.
void buildQuickRows(json::Value& root, std::vector<QuickRow>& rows);

// Every value a row can change (for the "modified" marker and for revert).
std::vector<json::Value*> rowValues(const QuickRow& r);

struct BulkAction {
    std::string label;
    std::string help;
    // apply == false: only count; returns the number of values that change / entries added.
    int (*run)(json::Value& root, bool apply);
};

// Labels follow the current UI language.
const std::vector<BulkAction>& bulkActions();

// Short summary for the footer, e.g. "Różańce 1234 · Maski 7".
std::string summary(json::Value& root);

// In-game English name of an entry from the save ("Lava Charm" -> "Magma Bell").
// Returns "" when the name is unknown or the same as the internal one.
std::string displayName(const std::string& internalName);

// Highest value the game uses for a playerData field (0 = unknown).
long long gameLimit(const std::string& playerDataKey);

// Optional definitions file (sdmc:/switch/SilksongSaveEditor/definitions.json) adding tools, crests,
// bosses and names, so game updates (e.g. the Sea of Sorrow expansion) work without a new .nro.
bool loadDefinitions(const std::vector<uint8_t>& data, std::string& err, int& added);

// Key facts of a save file, shown in the file list.
struct SlotPreview {
    bool ok = false;
    double playTime = -1;
    long long completion = -1;
    long long maxHealth = -1;
    long long silkMax = -1;
    long long geo = -1;
    long long shards = -1;
    long long permadeath = -1;
    std::string version;
    std::string scene;
};
SlotPreview previewSave(const std::vector<uint8_t>& file);

} // namespace silk
