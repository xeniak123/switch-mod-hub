#include "silksong.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "i18n.h"
#include "savecodec.h"

namespace silk {
namespace {

json::Value* playerData(json::Value& root) {
    json::Value* p = root.find("playerData");
    return (p && p->type == json::Type::Object) ? p : nullptr;
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

bool endsWith(const std::string& s, const char* p) {
    const size_t n = std::strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

bool contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

std::string spaced(std::string s) {
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}

// ---------------------------------------------------------------- definitions ---

struct StatDef {
    const char* key;
    const char* labelPl;
    const char* labelEn;
    const char* helpPl;
    const char* helpEn;
    long long minV;
    long long maxV;
    long long gameMax;
    const char* link1;
    const char* link2;
};

const StatDef kStats[] = {
    {"maxHealth", "Maski zdrowia (maksimum)", "Health masks (maximum)",
     "Maksymalne zdrowie. Ustawia też aktualne zdrowie (health) i bazę (maxHealthBase). Start gry: 5, po "
     "wszystkich ulepszeniach: 10.",
     "Maximum health. Also sets the current health (health) and the base (maxHealthBase). Start: 5, fully "
     "upgraded: 10.",
     1, 999, 10, "health", "maxHealthBase"},
    {"silkMax", "Jedwab (maksimum)", "Silk (maximum)",
     "Pojemność szpuli jedwabiu. Ustawia też aktualny jedwab (silk). Start gry: 9, po wszystkich ulepszeniach: 18.",
     "Silk spool capacity. Also sets the current silk (silk). Start: 9, fully upgraded: 18.", 0, 999, 18, "silk",
     nullptr},
    {"silkRegenMax", "Serca jedwabiu", "Silk hearts",
     "Liczba serc jedwabiu (regeneracja jedwabiu). W grze maksymalnie 3.",
     "Number of silk hearts (silk regeneration). The game has at most 3.", 0, 99, 3, nullptr, nullptr},
    {"geo", "Różańce (Rosaries)", "Rosaries", "Główna waluta w grze.", "The main currency.", 0, 2147483647LL, 0,
     nullptr, nullptr},
    {"ShellShards", "Odłamki muszli (Shell Shards)", "Shell Shards",
     "Materiał do wytwarzania i uzupełniania narzędzi.", "Material for crafting and refilling tools.", 0,
     2147483647LL, 0, nullptr, nullptr},
    {"nailUpgrades", "Ulepszenia igły", "Needle upgrades",
     "Poziom ulepszenia igły (0 = podstawowa, w grze maksymalnie 4).",
     "Needle upgrade level (0 = basic, the game has at most 4).", 0, 99, 4, nullptr, nullptr},
    {"heartPieces", "Fragmenty masek", "Mask shards", "Zebrane fragmenty masek (4 fragmenty = 1 maska).",
     "Collected mask shards (4 shards = 1 mask).", 0, 99, 0, nullptr, nullptr},
    {"silkSpoolParts", "Fragmenty szpuli", "Spool fragments",
     "Zebrane fragmenty szpuli jedwabiu (2 fragmenty = 1 szpula).",
     "Collected silk spool fragments (2 fragments = 1 spool).", 0, 99, 0, nullptr, nullptr},
    {"ToolPouchUpgrades", "Ulepszenia sakwy narzędzi", "Tool pouch upgrades",
     "Liczba ulepszeń sakwy (pojemność narzędzi).", "Number of pouch upgrades (tool capacity).", 0, 99, 0, nullptr,
     nullptr},
    {"ToolKitUpgrades", "Ulepszenia zestawu narzędzi", "Crafting kit upgrades",
     "Liczba ulepszeń zestawu do wytwarzania.", "Number of crafting kit upgrades.", 0, 99, 0, nullptr, nullptr},
    {"attunementLevel", "Poziom dostrojenia", "Attunement level", "Poziom dostrojenia (attunementLevel).",
     "Attunement level (attunementLevel).", 0, 99, 0, nullptr, nullptr},
    {"completionPercentage", "Procent ukończenia", "Completion percentage",
     "Procent ukończenia pokazywany w menu zapisu.", "Completion shown in the save menu.", 0, 1000, 0, nullptr,
     nullptr},
};

struct ToggleDef {
    const char* key;
    const char* labelPl;
    const char* labelEn;
    const char* helpPl;
    const char* helpEn;
};

const ToggleDef kToggles[] = {
    {"infiniteAirJump", "Nieskończony skok w powietrzu", "Infinite air jump",
     "Pozwala skakać w powietrzu bez ograniczeń (cheat).", "Allows unlimited jumps in the air (cheat)."},
    {"IsSilkSpoolBroken", "Szpula jedwabiu pęknięta", "Silk spool broken",
     "Stan pękniętej szpuli jedwabiu (element fabuły).", "Broken silk spool state (story element)."},
    {"UnlockedExtraBlueSlot", "Dodatkowy niebieski slot", "Extra blue slot",
     "Odblokowuje dodatkowy slot na niebieskie narzędzia.", "Unlocks an extra slot for blue tools."},
    {"UnlockedExtraYellowSlot", "Dodatkowy żółty slot", "Extra yellow slot",
     "Odblokowuje dodatkowy slot na żółte narzędzia.", "Unlocks an extra slot for yellow tools."},
    {"hasJournal", "Dziennik łowcy", "Hunter's Journal", "Dostęp do dziennika przeciwników (Hunter's Journal).",
     "Access to the enemy journal."},
    {"mapAllRooms", "Mapa: wszystkie pomieszczenia", "Map: all rooms", "Pokazuje na mapie wszystkie pomieszczenia.",
     "Shows every room on the map."},
};

struct AbilityDef {
    const char* key;
    const char* labelPl;
    const char* labelEn;
};

const AbilityDef kAbilities[] = {
    {"hasDash", "Swift Step (sprint)", "Swift Step (dash)"},
    {"hasBrolly", "Drifter's Cloak (szybowanie)", "Drifter's Cloak (glide)"},
    {"hasWalljump", "Cling Grip (wspinanie po ścianach)", "Cling Grip (wall jump)"},
    {"hasHarpoonDash", "Clawline (harpun)", "Clawline (harpoon)"},
    {"hasDoubleJump", "Faydown Cloak (podwójny skok)", "Faydown Cloak (double jump)"},
    {"hasSuperJump", "Silk Soar (super skok)", "Silk Soar (super jump)"},
    {"hasChargeSlash", "Needle Strike (cios naładowany)", "Needle Strike (charged attack)"},
    {"hasNeedolin", "Needolin", "Needolin"},
};

struct BossDef {
    std::string labelPl;
    std::string labelEn;
    std::string key;
};

const char* const kBuiltinBosses[][3] = {
    {"Moss Mother", "Moss Mother", "defeatedMossMother"},
    {"Moss Evolver", "Moss Evolver", "defeatedMossEvolver"},
    {"Bonetown Boss", "Bonetown Boss", "DefeatedBonetownBoss"},
    {"Skull King", "Skull King", "skullKingDefeated"},
    {"Bell Beast", "Bell Beast", "defeatedBellBeast"},
    {"Ant Queen", "Ant Queen", "defeatedAntQueen"},
    {"Lace (pierwsza walka)", "Lace (first fight)", "defeatedLace1"},
    {"Song Golem", "Song Golem", "defeatedSongGolem"},
    {"Dock Foremen", "Dock Foremen", "defeatedDockForemen"},
    {"Bone Flyer Giant", "Bone Flyer Giant", "defeatedBoneFlyerGiant"},
    {"Ant Trapper", "Ant Trapper", "defeatedAntTrapper"},
    {"Vampire Gnat", "Vampire Gnat", "defeatedVampireGnatBoss"},
    {"Crow Court", "Crow Court", "defeatedCrowCourt"},
    {"Wisp Pyre Effigy", "Wisp Pyre Effigy", "defeatedWispPyreEffigy"},
    {"Roof Crab", "Roof Crab", "roofCrabDefeated"},
    {"Spinner", "Spinner", "spinnerDefeated"},
    {"Splinter Queen", "Splinter Queen", "defeatedSplinterQueen"},
    {"Seth", "Seth", "defeatedSeth"},
    {"Flower Queen", "Flower Queen", "defeatedFlowerQueen"},
    {"Roachkeeper Chef", "Roachkeeper Chef", "defeatedRoachkeeperChef"},
    {"Phantom", "Phantom", "defeatedPhantom"},
    {"Swamp Shaman", "Swamp Shaman", "DefeatedSwampShaman"},
    {"Coral Drillers", "Coral Drillers", "defeatedCoralDrillers"},
    {"Coral King", "Coral King", "defeatedCoralKing"},
    {"Last Judge", "Last Judge", "defeatedLastJudge"},
    {"First Weaver", "First Weaver", "defeatedFirstWeaver"},
    {"Brood Mother", "Brood Mother", "defeatedBroodMother"},
    {"Trobbio", "Trobbio", "defeatedTrobbio"},
    {"Tormented Trobbio", "Tormented Trobbio", "defeatedTormentedTrobbio"},
    {"Cogwork Dancers", "Cogwork Dancers", "defeatedCogworkDancers"},
    {"Lace (wieża)", "Lace (tower)", "defeatedLaceTower"},
    {"Ward Boss", "Ward Boss", "wardBossDefeated"},
    {"Song Chevalier", "Song Chevalier", "defeatedSongChevalierBoss"},
    {"White Cloverstag", "White Cloverstag", "defeatedWhiteCloverstag"},
    {"Clover Dancers", "Clover Dancers", "defeatedCloverDancers"},
};

struct ToolDef {
    std::string name;
    bool hidden;
    int amount;
};

const struct {
    const char* name;
    bool hidden;
    int amount;
} kBuiltinTools[] = {
    {"Bone Necklace", false, 0},      {"Silk Spear", false, 0},          {"Compass", false, 0},
    {"Mosscreep Tool 1", true, 0},    {"Straight Pin", false, 12},       {"Bell Bind", false, 0},
    {"Rosary Magnet", false, 0},      {"Tri Pin", false, 20},            {"Thread Sphere", false, 0},
    {"Flea Brew", false, 6},          {"Harpoon", false, 17},            {"Dead Mans Purse", false, 0},
    {"Extractor", true, 0},           {"Lifeblood Syringe", false, 6},   {"Barbed Wire", false, 0},
    {"Poison Pouch", false, 0},       {"Sting Shard", false, 12},        {"Lava Charm", false, 0},
    {"Weighted Anklet", false, 0},    {"Thief Charm", false, 0},         {"Screw Attack", false, 13},
    {"Quickbind", false, 0},          {"Brolly Spike", false, 0},        {"Cogwork Saw", false, 16},
    {"Scuttlebrace", false, 0},       {"Cogwork Flier", false, 8},       {"Dazzle Bind", true, 0},
    {"Parry", false, 0},              {"Silk Bomb", false, 0},           {"Silk Snare", true, 8},
    {"Sprintmaster", false, 0},       {"Wallcling", false, 0},           {"Spool Extender", false, 0},
    {"Flintstone", false, 5},         {"Thief Claw", false, 0},          {"Magnetite Dice", false, 0},
    {"Conch Drill", false, 14},       {"Silk Charge", false, 0},         {"Rosary Cannon", false, 6},
    {"Pimpilo", false, 6},            {"Maggot Charm", false, 0},        {"Wisp Lantern", false, 0},
    {"WebShot Architect", false, 24}, {"Revenge Crystal", false, 0},     {"Zap Imbuement", false, 0},
    {"White Ring", false, 0},         {"Flea Charm", false, 0},          {"Multibind", false, 0},
    {"Shakra Ring", false, 20},       {"Tack", false, 10},               {"Musician Charm", false, 0},
    {"Dazzle Bind Upgraded", false, 0}, {"Pinstress Tool", false, 0},    {"Silk Boss Needle", false, 0},
    {"Mosscreep Tool 2", false, 0},   {"Curve Claws", false, 16},        {"Fractured Mask", false, 1},
    {"Longneedle", false, 0},         {"Quick Sling", false, 0},         {"Reserve Bind", false, 1},
    {"Lightning Rod", false, 10},
};

struct CrestDef {
    std::string name;
    int slots;
    bool addIfMissing;
};

const struct {
    const char* name;
    int slots;
} kBuiltinCrests[] = {
    {"Hunter", 7}, {"Reaper", 7}, {"Wanderer", 7}, {"Warrior", 5}, {"Witch", 6}, {"Toolmaster", 7}, {"Spell", 5},
};

// Internal save names -> in-game English names (from the game's English texts).
const char* const kNames[][2] = {
    // tools and silk skills
    {"Bone Necklace", "Shard Pendant"}, {"Silk Spear", "Silkspear"}, {"Mosscreep Tool 1", "Druid's Eye"},
    {"Bell Bind", "Warding Bell"}, {"Rosary Magnet", "Magnetite Brooch"}, {"Tri Pin", "Threefold Pin"},
    {"Thread Sphere", "Thread Storm"}, {"Harpoon", "Longpin"}, {"Dead Mans Purse", "Dead Bug's Purse"},
    {"Extractor", "Needle Phial"}, {"Lifeblood Syringe", "Plasmium Phial"}, {"Barbed Wire", "Barbed Bracelet"},
    {"Poison Pouch", "Pollip Pouch"}, {"Lava Charm", "Magma Bell"}, {"Weighted Anklet", "Weighted Belt"},
    {"Thief Charm", "Thief's Mark"}, {"Screw Attack", "Delver's Drill"}, {"Quickbind", "Injector Band"},
    {"Brolly Spike", "Sawtooth Circlet"}, {"Cogwork Saw", "Cogwork Wheel"}, {"Cogwork Flier", "Cogfly"},
    {"Dazzle Bind", "Claw Mirror"}, {"Parry", "Cross Stitch"}, {"Silk Bomb", "Rune Rage"},
    {"Silk Snare", "Snare Setter"}, {"Sprintmaster", "Silkspeed Anklets"}, {"Wallcling", "Ascendant's Grip"},
    {"Flintstone", "Flintslate"}, {"Thief Claw", "Snitch Pick"}, {"Conch Drill", "Conchcutter"},
    {"Silk Charge", "Sharpdart"}, {"Pimpilo", "Pimpillo"}, {"Maggot Charm", "Wreath of Purity"},
    {"Wisp Lantern", "Wispfire Lantern"}, {"WebShot Architect", "Silkshot"}, {"Revenge Crystal", "Memory Crystal"},
    {"Zap Imbuement", "Volt Filament"}, {"White Ring", "Weavelight"}, {"Flea Charm", "Egg of Flealia"},
    {"Multibind", "Multibinder"}, {"Shakra Ring", "Throwing Ring"}, {"Tack", "Tacks"},
    {"Musician Charm", "Spider Strings"}, {"Dazzle Bind Upgraded", "Claw Mirrors"}, {"Pinstress Tool", "Pin Badge"},
    {"Silk Boss Needle", "Pale Nails"}, {"Mosscreep Tool 2", "Druid's Eyes"}, {"Curve Claws", "Curveclaw"},
    {"Longneedle", "Longclaw"}, {"Lightning Rod", "Voltvessels"},
    // crests
    {"Warrior", "Beast"}, {"Toolmaster", "Architect"}, {"Spell", "Shaman"}, {"Hunter_v2", "Hunter v2"},
    {"Hunter_v3", "Hunter v3"},
    // collectables and key items
    {"Ant Trapper Item", "Grass Doll"}, {"Architect Key", "Architect's Key"}, {"Beastfly Remains", "Horn Fragment"},
    {"Broken SilkShot", "Ruined Tool"}, {"Broodmother Remains", "Broodmother's Eye"},
    {"Cog Heart Pieces", "Cogheart Pieces"}, {"Courier Supplies Slave", "Queen's Egg"},
    {"Courier Supplies Mask Maker", "Liquid Lacquer"}, {"Courier Supplies", "Courier's Swag"},
    {"Crow Feather", "Ragpelt"}, {"Dock Key", "Diving Bell Key"}, {"Enemy Morsel Seared", "Seared Organ"},
    {"Enemy Morsel Shredded", "Shredded Organ"}, {"Enemy Morsel Speared", "Skewered Organ"},
    {"Fixer Idol", "Hornet Statuette"}, {"Great Shard", "Beast Shard"}, {"Memento Garmond", "Hero's Memento"},
    {"Memento Surface", "Surface Memento"}, {"Plasmium Blood", "Plasmified Blood"},
    {"R Ancient Egg", "Arcane Egg"}, {"R Bone Record", "Bone Scroll"}, {"R Seal Chit", "Choral Commandment"},
    {"R Weaver Record", "Rune Harp"}, {"R Weaver Totem", "Weaver Effigy"}, {"R Psalm Cylinder", "Psalm Cylinder"},
    {"Roach Corpse Item", "Roach Guts"}, {"Rock Roller Item", "Flintgem"}, {"Shard Pouch", "Shard Bundle"},
    {"Shell Flower", "Pollip Heart"}, {"Silk Grub", "Silkeater"}, {"Snare Soul Churchkeeper", "Maiden's Soul"},
    {"Snare Soul Bell Hermit", "Hermit's Soul"}, {"Snare Soul Swamp Bug", "Seeker's Soul"},
    {"Tool Metal", "Craftmetal"}, {"Ward Boss Key", "Surgeon's Key"}, {"Ward Key", "White Key"},
    {"White Flower", "Everbloom"}, {"Pilgrim Rag", "Pilgrim Shawl"},
    {"Tool Pouch&Kit Inv", "Tool Pouch & Crafting Kit"}, {"Coral Ingredient", "Crustnut"},
    {"Song Pilgrim Cloak", "Choir Cloak"}, {"Coral Heart", "Encrusted Heart"}, {"Hunter Heart", "Hunter's Heart"},
    {"Clover Heart", "Conjoined Heart"}, {"Crowman Memento", "Craw Memento"},
    {"Crest Socket Unlocker", "Memory Locket"}, {"Common Spine", "Spine Core"},
    {"Pickled Roach Egg", "Pickled Muckmaggot"}, {"Wood Witch Item", "Twisted Bud"},
    {"Skull King Fragment", "Crown Fragment"}, {"Silver Bellclapper", "Silver Bell"},
    {"Belltown House Key", "Bellhome Key"}, {"Slab Key", "Slab Keys"},
    // quests (wishes)
    {"Citadel Seeker", "The Great Citadel"}, {"Citadel Investigate", "Silent Halls"},
    {"Citadel Ascent", "Pharloom's Crown"}, {"Doctor Curse Cure", "Infestation Operation"},
    {"Garmond Black Threaded", "Hero's Call"}, {"Save Courier Short", "My Missing Courier"},
    {"Save Courier Tall", "My Missing Brother"}, {"Save Sherma", "Balm for the Wounded"},
    {"Shakra Final Quest", "Trail's End"}, {"Soul Snare", "Silk and Soul"}, {"Bellbeast Rescue", "Beast in the Bells"},
    {"Grand Gate Bellshrines", "Grand Gate"}, {"Black Thread Pt0", "After the Fall"},
    {"Black Thread Pt1 Shamans", "Awaiting the End"}, {"Black Thread Pt2 Abyss", "The Dark Below"},
    {"Black Thread Pt3 Escape", "Return to Pharloom"}, {"Black Thread Pt4 Return", "Spell Seeker"},
    {"Black Thread Pt5 Heart", "The Old Hearts"}, {"Black Thread Pt6 Flower", "Last Dive"},
    {"Diving Bell Pt1 Inspect", "The Dark Below"}, {"Diving Bell Pt2 Ballow", "The Dark Below"},
    {"Diving Bell Pt3 Descend", "The Dark Below"},
};

// Relic entries are "<type> <location>", e.g. "Bone Record Bone_East_14".
const char* const kRelicTypes[][2] = {
    {"Librarian Melody Cylinder", "Sacred Cylinder"},
    {"Psalm Cylinder", "Psalm Cylinder"},
    {"Bone Record", "Bone Scroll"},
    {"Seal Chit", "Choral Commandment"},
    {"Weaver Totem", "Weaver Effigy"},
    {"Weaver Record", "Rune Harp"},
    {"Ancient Egg", "Arcane Egg"},
};

// Built-in lists plus whatever the definitions file adds.
struct Registry {
    std::vector<ToolDef> tools;
    std::vector<CrestDef> crests;
    std::vector<BossDef> bosses;
    std::map<std::string, std::string> names;
    Registry() {
        for (const auto& t : kBuiltinTools) tools.push_back({t.name, t.hidden, t.amount});
        for (const auto& c : kBuiltinCrests) crests.push_back({c.name, c.slots, true});
        for (const auto& b : kBuiltinBosses) bosses.push_back({b[0], b[1], b[2]});
        for (const auto& n : kNames) names[n[0]] = n[1];
    }
};

Registry& reg() {
    static Registry r;
    return r;
}

// Crests that are story states / upgrade stages and must not be force-unlocked in bulk.
bool crestExcludedFromBulk(const std::string& name) {
    return name == "Cursed" || name == "Hunter_v2" || name == "Hunter_v3";
}

// ---------------------------------------------------------------- helpers ---

json::Value* field(json::Value* obj, const char* key) { return obj ? obj->find(key) : nullptr; }

json::Value* boolField(json::Value* obj, const char* key) {
    json::Value* v = field(obj, key);
    return (v && v->type == json::Type::Bool) ? v : nullptr;
}

json::Value* savedList(json::Value& root, const char* section, const char* listKey = "savedData") {
    json::Value* sec = field(playerData(root), section);
    json::Value* list = field(sec, listKey);
    return (list && list->type == json::Type::Array) ? list : nullptr;
}

const std::string* entryName(json::Value& e) {
    json::Value* n = e.find("Name");
    return (n && n->type == json::Type::String) ? &n->text : nullptr;
}

json::Value* findByName(json::Value& list, const std::string& name) {
    for (json::Value& e : list.items) {
        const std::string* n = entryName(e);
        if (n && *n == name) return &e;
    }
    return nullptr;
}

std::string nameOrDisplay(const std::string& internal) {
    const std::string d = displayName(internal);
    return d.empty() ? spaced(internal) : d;
}

bool isAbilityKey(const std::string& k) {
    return startsWith(k, "has") && k != "hasJournal" && !startsWith(k, "hasPin") && !startsWith(k, "hasMarker");
}

bool isFastTravelKey(const std::string& k) {
    if (k == "bellCentipedeAppeared") return true;
    return startsWith(k, "Unlocked") && !contains(k, "Slot") && !contains(k, "Melody");
}

bool isBossFlag(const std::string& k) {
    return startsWith(k, "defeated") || startsWith(k, "Defeated") || endsWith(k, "Defeated");
}

int setB(json::Value* v, bool b, bool apply) {
    if (!v || v->type != json::Type::Bool || v->boolean == b) return 0;
    if (apply) json::setBool(*v, b);
    return 1;
}

void resetDefaults(json::Value& v) {
    switch (v.type) {
        case json::Type::Bool: v.boolean = false; break;
        case json::Type::Number: v.text = "0"; break;
        case json::Type::String:
            v.text.clear();
            v.raw.clear();
            break;
        case json::Type::Array: v.items.clear(); break;
        case json::Type::Object:
            for (json::Member& m : v.members) resetDefaults(m.value);
            break;
        case json::Type::Null: break;
    }
}

void overlay(json::Value& dst, const json::Value& src) {
    if (dst.type != json::Type::Object || src.type != json::Type::Object) {
        dst = src;
        return;
    }
    for (const json::Member& m : src.members) {
        json::Value* d = dst.find(m.key);
        if (d && d->type == json::Type::Object && m.value.type == json::Type::Object) overlay(*d, m.value);
        else if (d) *d = m.value;
        else json::addMember(dst, m.key, m.value);
    }
}

// New list entry shaped like the existing entries (unknown fields reset to defaults).
json::Value makeEntryLike(const json::Value& list, const json::Value& tmpl) {
    json::Value e = tmpl;
    if (!list.items.empty() && list.items[0].type == json::Type::Object) {
        e = list.items[0];
        json::clearModified(e);
        resetDefaults(e);
        overlay(e, tmpl);
    }
    e.added = true;
    return e;
}

json::Value toolTemplate(const ToolDef& t) {
    json::Value o = json::makeObject();
    json::addMember(o, "Name", json::makeString(t.name));
    json::Value d = json::makeObject();
    json::addMember(d, "IsUnlocked", json::makeBool(true));
    json::addMember(d, "IsHidden", json::makeBool(t.hidden));
    json::addMember(d, "HasBeenSeen", json::makeBool(true));
    json::addMember(d, "HasBeenSelected", json::makeBool(false));
    json::addMember(d, "AmountLeft", json::makeInt(t.amount));
    json::addMember(o, "Data", d);
    return o;
}

const json::Value* findSlotTemplate(json::Value& list) {
    for (json::Value& e : list.items) {
        json::Value* slots = field(field(&e, "Data"), "Slots");
        if (slots && slots->type == json::Type::Array && !slots->items.empty() &&
            slots->items[0].type == json::Type::Object)
            return &slots->items[0];
    }
    return nullptr;
}

json::Value crestTemplate(const CrestDef& c, const json::Value* slotTmpl) {
    json::Value o = json::makeObject();
    json::addMember(o, "Name", json::makeString(c.name));
    json::Value d = json::makeObject();
    json::addMember(d, "IsUnlocked", json::makeBool(true));
    json::Value slots = json::makeArray();
    for (int i = 0; i < c.slots; ++i) {
        json::Value s;
        if (slotTmpl) {
            s = *slotTmpl;
            json::clearModified(s);
            resetDefaults(s);
        } else {
            s = json::makeObject();
        }
        json::Value* u = s.find("IsUnlocked");
        if (u) *u = json::makeBool(true);
        else json::addMember(s, "IsUnlocked", json::makeBool(true));
        slots.items.push_back(s);
    }
    json::addMember(d, "Slots", slots);
    json::addMember(d, "DisplayNewIndicator", json::makeBool(false));
    json::addMember(o, "Data", d);
    return o;
}

// ---------------------------------------------------------------- bulk actions ---

int actFullHealth(json::Value& root, bool apply) {
    json::Value* p = playerData(root);
    if (!p) return 0;
    int n = 0;
    const std::pair<const char*, const char*> pairs[] = {{"maxHealth", "health"}, {"silkMax", "silk"}};
    for (const auto& pr : pairs) {
        json::Value* mx = p->find(pr.first);
        json::Value* cur = p->find(pr.second);
        if (mx && cur && json::isIntegerNumber(*mx) && json::isIntegerNumber(*cur) &&
            json::asInt(*mx) != json::asInt(*cur)) {
            ++n;
            if (apply) json::setInt(*cur, json::asInt(*mx));
        }
    }
    return n;
}

int actByKey(json::Value& root, bool apply, bool (*pred)(const std::string&)) {
    json::Value* p = playerData(root);
    if (!p) return 0;
    int n = 0;
    for (json::Member& m : p->members)
        if (pred(m.key)) n += setB(&m.value, true, apply);
    return n;
}

int actAbilities(json::Value& root, bool apply) { return actByKey(root, apply, isAbilityKey); }

int actMaps(json::Value& root, bool apply) {
    return actByKey(root, apply, [](const std::string& k) { return startsWith(k, "Has") && endsWith(k, "Map"); });
}

int actPins(json::Value& root, bool apply) {
    return actByKey(root, apply,
                    [](const std::string& k) { return startsWith(k, "hasPin") || startsWith(k, "hasMarker"); });
}

int actFastTravel(json::Value& root, bool apply) { return actByKey(root, apply, isFastTravelKey); }

int actFleas(json::Value& root, bool apply) {
    return actByKey(root, apply, [](const std::string& k) { return startsWith(k, "SavedFlea_"); });
}

int actTools(json::Value& root, bool apply) {
    json::Value* list = savedList(root, "Tools");
    if (!list) return 0;
    int n = 0;
    // existing entries
    for (json::Value& e : list->items) {
        json::Value* d = field(&e, "Data");
        int c = setB(field(d, "IsUnlocked"), true, apply);
        c += setB(field(d, "HasBeenSeen"), true, apply);
        n += c ? 1 : 0;
    }
    // missing entries
    for (const ToolDef& t : reg().tools) {
        if (findByName(*list, t.name)) continue;
        ++n;
        if (apply) {
            json::Value e = makeEntryLike(*list, toolTemplate(t));
            list->items.push_back(e);
        }
    }
    return n;
}

int actCrests(json::Value& root, bool apply) {
    int n = 0;
    json::Value* p = playerData(root);
    if (p) {
        n += setB(p->find("UnlockedExtraBlueSlot"), true, apply);
        n += setB(p->find("UnlockedExtraYellowSlot"), true, apply);
    }
    json::Value* list = savedList(root, "ToolEquips");
    if (!list) return n;
    for (json::Value& e : list->items) {
        const std::string* name = entryName(e);
        if (!name || crestExcludedFromBulk(*name)) continue;
        json::Value* d = field(&e, "Data");
        int c = setB(field(d, "IsUnlocked"), true, apply);
        json::Value* slots = field(d, "Slots");
        if (slots && slots->type == json::Type::Array)
            for (json::Value& s : slots->items) c += setB(s.find("IsUnlocked"), true, apply);
        n += c ? 1 : 0;
    }
    const json::Value* slotTmpl = findSlotTemplate(*list);
    json::Value slotCopy;
    if (slotTmpl) slotCopy = *slotTmpl;  // copy: pushing entries may reallocate the list
    for (const CrestDef& c : reg().crests) {
        if (!c.addIfMissing || findByName(*list, c.name)) continue;
        ++n;
        if (apply) {
            json::Value e = makeEntryLike(*list, crestTemplate(c, slotTmpl ? &slotCopy : nullptr));
            list->items.push_back(e);
        }
    }
    return n;
}

int actJournalSeen(json::Value& root, bool apply) {
    json::Value* list = savedList(root, "EnemyJournalKillData", "list");
    if (!list) return 0;
    int n = 0;
    for (json::Value& e : list->items) {
        json::Value* rec = field(&e, "Record");
        json::Value* kills = field(rec, "Kills");
        int c = setB(boolField(rec, "HasBeenSeen"), true, apply);
        if (kills && json::isIntegerNumber(*kills) && json::asInt(*kills) < 1) {
            ++c;
            if (apply) json::setInt(*kills, 1);
        }
        n += c ? 1 : 0;
    }
    return n;
}

// ---------------------------------------------------------------- quick rows ---

void addIntRow(std::vector<QuickRow>& rows, json::Value* pd, const StatDef& s) {
    json::Value* v = pd->find(s.key);
    if (!v || v->type != json::Type::Number) return;
    QuickRow r;
    r.kind = json::isIntegerNumber(*v) ? RowKind::Int : RowKind::Float;
    r.label = tr(s.labelPl, s.labelEn);
    r.key = std::string("playerData.") + s.key;
    r.help = tr(s.helpPl, s.helpEn);
    r.value = v;
    r.minValue = s.minV;
    r.maxValue = s.maxV;
    r.gameMax = s.gameMax;
    for (const char* l : {s.link1, s.link2}) {
        if (!l) continue;
        json::Value* lv = pd->find(l);
        if (lv && json::isIntegerNumber(*lv)) r.linked.push_back(lv);
    }
    rows.push_back(r);
}

void addHeader(std::vector<QuickRow>& rows, const std::string& label, const std::string& help = "") {
    QuickRow r;
    r.kind = RowKind::Header;
    r.label = label;
    r.help = help;
    rows.push_back(r);
}

QuickRow boolRow(json::Value* v, const std::string& label, const std::string& key, const std::string& help) {
    QuickRow r;
    r.kind = RowKind::Bool;
    r.label = label;
    r.key = key;
    r.help = help;
    r.value = v;
    return r;
}

// Removes a header that got no rows.
void closeSection(std::vector<QuickRow>& rows, size_t before) {
    if (rows.size() == before + 1) rows.pop_back();
}

void addSteelSoulRow(std::vector<QuickRow>& rows, json::Value* pd) {
    json::Value* v = pd->find("permadeathMode");
    if (!v || !json::isIntegerNumber(*v)) return;
    QuickRow r;
    r.kind = RowKind::Choice;
    r.label = tr("Tryb gry (Steel Soul)", "Game mode (Steel Soul)");
    r.key = "playerData.permadeathMode";
    r.help = tr("W trybie Steel Soul śmierć niszczy zapis (wartość 2). Zmiana z 2 na 1 powinna przywrócić taki zapis – "
                "tak działało to w Hollow Knight, w Silksongu nie zostało to sprawdzone. Zrób kopię przed zmianą.",
                "In Steel Soul mode death shatters the save (value 2). Changing 2 back to 1 should restore such a "
                "save - that is how it worked in Hollow Knight; it has not been verified in Silksong. Keep a backup.");
    r.value = v;
    r.choices = {tr("Zwykły", "Normal"), "Steel Soul", tr("Steel Soul – zapis zniszczony", "Steel Soul - shattered")};
    r.getChoice = [v] {
        const long long m = json::asInt(*v);
        return (m >= 0 && m <= 2) ? static_cast<int>(m) : -1;
    };
    r.setChoice = [v](int c) { json::setInt(*v, c); };
    r.touched = {v};
    r.touchedNames = {"permadeathMode"};
    rows.push_back(r);
}

void addQuestRows(std::vector<QuickRow>& rows, json::Value& root) {
    json::Value* list = savedList(root, "QuestCompletionData");
    if (!list) return;
    const size_t before = rows.size();
    addHeader(rows, tr("Zadania (Wishes)", "Quests (wishes)"),
              tr("Stan zadań. „Ukończone” nie daje nagrody – zmienia tylko stan zadania w zapisie.",
                 "Quest states. \"Completed\" gives no reward - it only changes the state stored in the save."));
    for (size_t i = 0; i < list->items.size(); ++i) {
        json::Value& e = list->items[i];
        const std::string* name = entryName(e);
        json::Value* d = field(&e, "Data");
        json::Value* seen = boolField(d, "HasBeenSeen");
        json::Value* acc = boolField(d, "IsAccepted");
        json::Value* comp = boolField(d, "IsCompleted");
        if (!name || !seen || !acc || !comp) continue;
        json::Value* ever = boolField(d, "WasEverCompleted");
        json::Value* count = field(d, "CompletedCount");
        if (count && !json::isIntegerNumber(*count)) count = nullptr;
        QuickRow r;
        r.kind = RowKind::Choice;
        r.label = nameOrDisplay(*name);
        r.key = "QuestCompletionData[" + std::to_string(i) + "] · " + *name;
        r.help = std::string(tr("Zadanie „", "Quest \"")) + r.label +
                 tr("”. „Ukończone” nie daje nagrody – zmienia tylko stan zadania w zapisie.",
                    "\". \"Completed\" gives no reward - it only changes the state stored in the save.");
        r.value = comp;
        r.choices = {tr("Nieznane", "Not encountered"), tr("Widziane", "Seen"), tr("Przyjęte", "Accepted"),
                     tr("Ukończone", "Completed")};
        r.getChoice = [seen, acc, comp] {
            if (comp->boolean) return 3;
            if (acc->boolean) return 2;
            return seen->boolean ? 1 : 0;
        };
        r.setChoice = [seen, acc, comp, ever, count](int c) {
            json::setBool(*seen, c >= 1);
            json::setBool(*acc, c >= 2);
            json::setBool(*comp, c >= 3);
            if (ever) {
                if (c >= 3) json::setBool(*ever, true);
                else json::revert(*ever);
            }
            if (count) {
                if (c >= 3) {
                    if (json::asInt(*count) < 1) json::setInt(*count, 1);
                } else {
                    json::revert(*count);
                }
            }
        };
        r.touched = {seen, acc, comp};
        r.touchedNames = {"HasBeenSeen", "IsAccepted", "IsCompleted"};
        if (ever) {
            r.touched.push_back(ever);
            r.touchedNames.push_back("WasEverCompleted");
        }
        if (count) {
            r.touched.push_back(count);
            r.touchedNames.push_back("CompletedCount");
        }
        rows.push_back(r);
    }
    closeSection(rows, before);
}

void addRelicRows(std::vector<QuickRow>& rows, json::Value& root) {
    json::Value* list = savedList(root, "Relics");
    if (!list) return;
    const size_t before = rows.size();
    addHeader(rows, tr("Relikwie", "Relics"));
    for (size_t i = 0; i < list->items.size(); ++i) {
        json::Value& e = list->items[i];
        const std::string* name = entryName(e);
        json::Value* d = field(&e, "Data");
        json::Value* col = boolField(d, "IsCollected");
        json::Value* dep = boolField(d, "IsDeposited");
        json::Value* seen = boolField(d, "HasSeenInRelicBoard");
        if (!name || !col || !dep) continue;
        QuickRow r;
        r.kind = RowKind::Choice;
        r.label = nameOrDisplay(*name);
        r.key = "Relics[" + std::to_string(i) + "] · " + *name;
        r.help = std::string(tr("Relikwia „", "Relic \"")) + r.label +
                 tr("”. Oddana = przekazana kolekcjonerowi, obejrzana = obejrzana na tablicy relikwii.",
                    "\". Deposited = handed to the collector, seen = viewed on the relic board.");
        r.value = col;
        r.choices = {tr("Brak", "Not found"), tr("Zebrana", "Collected"), tr("Oddana", "Deposited")};
        if (seen) r.choices.push_back(tr("Obejrzana", "Seen"));
        r.getChoice = [col, dep, seen] {
            if (seen && seen->boolean) return 3;
            if (dep->boolean) return 2;
            return col->boolean ? 1 : 0;
        };
        r.setChoice = [col, dep, seen](int c) {
            json::setBool(*col, c >= 1);
            json::setBool(*dep, c >= 2);
            if (seen) json::setBool(*seen, c >= 3);
        };
        r.touched = {col, dep};
        r.touchedNames = {"IsCollected", "IsDeposited"};
        if (seen) {
            r.touched.push_back(seen);
            r.touchedNames.push_back("HasSeenInRelicBoard");
        }
        rows.push_back(r);
    }
    closeSection(rows, before);
}

void addJournalRows(std::vector<QuickRow>& rows, json::Value& root) {
    json::Value* list = savedList(root, "EnemyJournalKillData", "list");
    if (!list) return;
    const size_t before = rows.size();
    addHeader(rows, tr("Dziennik łowcy (pokonani)", "Hunter's Journal (kills)"));
    for (size_t i = 0; i < list->items.size(); ++i) {
        json::Value& e = list->items[i];
        const std::string* name = entryName(e);
        json::Value* rec = field(&e, "Record");
        json::Value* kills = field(rec, "Kills");
        if (!name || !kills || !json::isIntegerNumber(*kills)) continue;
        QuickRow r;
        r.kind = RowKind::Int;
        r.label = *name;
        r.key = "EnemyJournalKillData[" + std::to_string(i) + "].Record.Kills";
        r.help = std::string(tr("Liczba pokonanych: „", "Kills: \"")) + *name +
                 tr("”. Wartość powyżej 0 oznacza też wpis jako widziany.",
                    "\". A value above 0 also marks the entry as seen.");
        r.value = kills;
        r.maxValue = 9999;
        if (json::Value* s = boolField(rec, "HasBeenSeen")) r.whenPositive.push_back({s, true});
        rows.push_back(r);
    }
    closeSection(rows, before);
}

void addCrestSlotRows(std::vector<QuickRow>& rows, json::Value& list) {
    const size_t before = rows.size();
    addHeader(rows, tr("Sloty herbów", "Crest slots"));
    for (size_t i = 0; i < list.items.size(); ++i) {
        json::Value& e = list.items[i];
        const std::string* name = entryName(e);
        json::Value* slots = field(field(&e, "Data"), "Slots");
        if (!name || !slots || slots->type != json::Type::Array) continue;
        const std::string crest = nameOrDisplay(*name);
        for (size_t j = 0; j < slots->items.size(); ++j) {
            json::Value* u = boolField(&slots->items[j], "IsUnlocked");
            if (!u) continue;
            rows.push_back(boolRow(u, crest + tr(" – slot ", " - slot ") + std::to_string(j + 1),
                                   "ToolEquips[" + std::to_string(i) + "].Data.Slots[" + std::to_string(j) + "]",
                                   std::string(tr("Slot ", "Slot ")) + std::to_string(j + 1) + tr(" herbu „", " of the \"") +
                                       crest + tr("”.", "\" crest.")));
        }
    }
    closeSection(rows, before);
}

} // namespace

std::string displayName(const std::string& internalName) {
    const Registry& r = reg();
    auto it = r.names.find(internalName);
    if (it != r.names.end()) return it->second == internalName ? "" : it->second;
    for (const auto& t : kRelicTypes) {
        const std::string prefix = t[0];
        if (internalName == prefix) return t[1];
        if (internalName.size() > prefix.size() + 1 && internalName.compare(0, prefix.size(), prefix) == 0 &&
            internalName[prefix.size()] == ' ')
            return std::string(t[1]) + " – " + spaced(internalName.substr(prefix.size() + 1));
    }
    return "";
}

long long gameLimit(const std::string& key) {
    for (const StatDef& s : kStats)
        if (key == s.key) return s.gameMax;
    return 0;
}

std::vector<json::Value*> rowValues(const QuickRow& r) {
    std::vector<json::Value*> v;
    auto add = [&v](json::Value* p) {
        if (p && std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);
    };
    add(r.value);
    for (json::Value* l : r.linked) add(l);
    for (const auto& p : r.whenOn) add(p.first);
    for (const auto& p : r.whenChanged) add(p.first);
    for (const auto& p : r.whenPositive) add(p.first);
    for (json::Value* t : r.touched) add(t);
    return v;
}

void buildQuickRows(json::Value& root, std::vector<QuickRow>& rows) {
    rows.clear();
    json::Value* pd = playerData(root);
    if (!pd) {
        addHeader(rows, tr("Brak sekcji playerData", "No playerData section"),
                  tr("Ten plik nie wygląda na zapis Silksong. Wszystkie wartości możesz nadal edytować w karcie "
                     "„Wszystkie dane”.",
                     "This file does not look like a Silksong save. You can still edit every value on the "
                     "\"All data\" tab."));
        return;
    }

    addHeader(rows, tr("Postać i waluta", "Character and currency"));
    for (const StatDef& s : kStats) addIntRow(rows, pd, s);
    if (json::Value* t = pd->find("playTime")) {
        if (t->type == json::Type::Number) {
            QuickRow r;
            r.kind = json::isIntegerNumber(*t) ? RowKind::Int : RowKind::Float;
            r.label = tr("Czas gry", "Play time");
            r.key = "playerData.playTime";
            r.help = tr("Czas gry w sekundach (lewo/prawo zmienia o sekundy, przytrzymaj, aby przyspieszyć).",
                        "Play time in seconds (left/right changes it by seconds, hold to speed up).");
            r.value = t;
            r.isTime = true;
            r.maxValue = 2147483647LL;
            rows.push_back(r);
        }
    }
    addSteelSoulRow(rows, pd);

    size_t before = rows.size();
    addHeader(rows, tr("Przełączniki", "Switches"));
    for (const ToggleDef& t : kToggles) {
        json::Value* v = pd->find(t.key);
        if (v && v->type == json::Type::Bool)
            rows.push_back(boolRow(v, tr(t.labelPl, t.labelEn), std::string("playerData.") + t.key, tr(t.helpPl, t.helpEn)));
    }
    closeSection(rows, before);

    before = rows.size();
    addHeader(rows, tr("Umiejętności", "Abilities"));
    for (json::Member& m : pd->members) {
        if (m.value.type != json::Type::Bool || !isAbilityKey(m.key)) continue;
        std::string label = m.key;
        for (const AbilityDef& a : kAbilities)
            if (m.key == a.key) label = tr(a.labelPl, a.labelEn);
        rows.push_back(boolRow(&m.value, label, "playerData." + m.key,
                               std::string(tr("Umiejętność postaci (flaga ", "Character ability (flag ")) + m.key + ")."));
    }
    closeSection(rows, before);

    if (json::Value* list = savedList(root, "Tools")) {
        before = rows.size();
        addHeader(rows, tr("Narzędzia", "Tools"));
        for (size_t i = 0; i < list->items.size(); ++i) {
            json::Value& e = list->items[i];
            const std::string* name = entryName(e);
            json::Value* d = field(&e, "Data");
            json::Value* u = boolField(d, "IsUnlocked");
            if (!name || !u) continue;
            const std::string label = nameOrDisplay(*name);
            QuickRow r = boolRow(u, label, "Tools[" + std::to_string(i) + "] · " + *name,
                                 std::string(tr("Narzędzie „", "Tool \"")) + label +
                                     tr("”. Włączenie oznacza je też jako zobaczone.",
                                        "\". Turning it on also marks it as seen."));
            if (json::Value* seen = boolField(d, "HasBeenSeen")) r.whenOn.push_back({seen, true});
            rows.push_back(r);
        }
        closeSection(rows, before);
    }

    if (json::Value* list = savedList(root, "ToolEquips")) {
        before = rows.size();
        addHeader(rows, tr("Herby (Crests)", "Crests"));
        for (size_t i = 0; i < list->items.size(); ++i) {
            json::Value& e = list->items[i];
            const std::string* name = entryName(e);
            json::Value* d = field(&e, "Data");
            json::Value* u = boolField(d, "IsUnlocked");
            if (!name || !u) continue;
            const std::string label = nameOrDisplay(*name);
            QuickRow r = boolRow(u, label, "ToolEquips[" + std::to_string(i) + "] · " + *name,
                                 std::string(tr("Herb „", "Crest \"")) + label +
                                     tr("”. Sloty herbów zmienisz w sekcji „Sloty herbów”.",
                                        "\". Its slots are in the \"Crest slots\" section."));
            if (json::Value* nw = boolField(d, "DisplayNewIndicator")) r.whenChanged.push_back({nw, false});
            rows.push_back(r);
        }
        closeSection(rows, before);
        addCrestSlotRows(rows, *list);
    }

    if (json::Value* list = savedList(root, "Collectables")) {
        before = rows.size();
        addHeader(rows, tr("Przedmioty (Collectables)", "Items (collectables)"));
        for (size_t i = 0; i < list->items.size(); ++i) {
            json::Value& e = list->items[i];
            const std::string* name = entryName(e);
            json::Value* a = field(field(&e, "Data"), "Amount");
            if (!name || !a || !json::isIntegerNumber(*a)) continue;
            QuickRow r;
            r.kind = RowKind::Int;
            r.label = nameOrDisplay(*name);
            r.key = "Collectables[" + std::to_string(i) + "] · " + *name;
            r.help = std::string(tr("Liczba posiadanych sztuk: „", "Number owned: \"")) + r.label + tr("”.", "\".");
            r.value = a;
            r.maxValue = 9999;
            rows.push_back(r);
        }
        closeSection(rows, before);
    }

    addRelicRows(rows, root);
    addQuestRows(rows, root);
    addJournalRows(rows, root);

    before = rows.size();
    addHeader(rows, tr("Mapy", "Maps"));
    for (json::Member& m : pd->members) {
        if (m.value.type == json::Type::Bool && startsWith(m.key, "Has") && endsWith(m.key, "Map"))
            rows.push_back(boolRow(&m.value, m.key, "playerData." + m.key, tr("Mapa obszaru.", "Area map.")));
    }
    closeSection(rows, before);

    before = rows.size();
    addHeader(rows, tr("Szybka podróż", "Fast travel"));
    for (json::Member& m : pd->members) {
        if (m.value.type == json::Type::Bool && isFastTravelKey(m.key))
            rows.push_back(boolRow(&m.value, m.key, "playerData." + m.key,
                                   tr("Stacja / punkt szybkiej podróży.", "Fast travel station / point.")));
    }
    closeSection(rows, before);

    before = rows.size();
    addHeader(rows, tr("Pchły", "Fleas"));
    for (json::Member& m : pd->members) {
        if (m.value.type == json::Type::Bool && startsWith(m.key, "SavedFlea_"))
            rows.push_back(boolRow(&m.value, m.key.substr(10), "playerData." + m.key, tr("Uratowana pchła.", "Rescued flea.")));
    }
    closeSection(rows, before);

    before = rows.size();
    addHeader(rows, tr("Bossowie (pokonani)", "Bosses (defeated)"),
              tr("Flagi fabularne. Zmieniaj ostrożnie – wyłączenie lub włączenie pokonania bossa może zmienić przebieg "
                 "fabuły.",
                 "Story flags. Change carefully - toggling a boss defeat can change the story."));
    const std::string bossHelp = tr("Flaga pokonania bossa. Zmieniaj ostrożnie – może wpłynąć na fabułę.",
                                    "Boss defeated flag. Change carefully - it can affect the story.");
    std::vector<std::string> listed;
    for (const BossDef& b : reg().bosses) {
        json::Value* v = pd->find(b.key);
        if (v && v->type == json::Type::Bool) {
            rows.push_back(boolRow(v, i18n::english() ? b.labelEn : b.labelPl, "playerData." + b.key, bossHelp));
            listed.push_back(b.key);
        }
    }
    // flags the list does not know yet (e.g. new bosses from a game update)
    for (json::Member& m : pd->members) {
        if (m.value.type != json::Type::Bool || !isBossFlag(m.key)) continue;
        if (std::find(listed.begin(), listed.end(), m.key) != listed.end()) continue;
        rows.push_back(boolRow(&m.value, m.key, "playerData." + m.key, bossHelp));
    }
    closeSection(rows, before);
}

const std::vector<BulkAction>& bulkActions() {
    static std::vector<BulkAction> cache[2];
    std::vector<BulkAction>& actions = cache[i18n::english() ? 1 : 0];
    if (actions.empty()) {
        actions = {
            {tr("Pełne zdrowie i jedwab", "Full health and silk"),
             tr("Ustawia aktualne zdrowie = maksimum masek oraz jedwab = maksimum jedwabiu.",
                "Sets the current health to the mask maximum and the silk to the silk maximum."),
             actFullHealth},
            {tr("Odblokuj wszystkie umiejętności", "Unlock all abilities"),
             tr("Włącza wszystkie flagi umiejętności (has...), np. sprint, szybowanie, harpun, podwójny skok. "
                "Nie zmienia dziennika ani pinezek mapy.",
                "Turns on every ability flag (has...), e.g. dash, glide, harpoon, double jump. Does not change the "
                "journal or the map pins."),
             actAbilities},
            {tr("Odblokuj wszystkie narzędzia", "Unlock all tools"),
             tr("Odblokowuje każde narzędzie (Tools). Brakujące narzędzia są dopisywane do zapisu z domyślną liczbą "
                "użyć.",
                "Unlocks every tool. Missing tools are added to the save with their default number of uses."),
             actTools},
            {tr("Odblokuj herby i wszystkie sloty", "Unlock crests and all slots"),
             tr("Odblokowuje podstawowe herby i wszystkie ich sloty oraz dodatkowe sloty niebieski i żółty. Herby "
                "fabularne (Cursed, ulepszenia Huntera) są pomijane.",
                "Unlocks the base crests with all their slots plus the extra blue and yellow slots. Story crests "
                "(Cursed, Hunter upgrades) are skipped."),
             actCrests},
            {tr("Wszystkie mapy obszarów", "All area maps"),
             tr("Włącza wszystkie flagi map (Has...Map).", "Turns on every map flag (Has...Map)."), actMaps},
            {tr("Wszystkie pinezki i znaczniki mapy", "All map pins and markers"),
             tr("Włącza flagi hasPin... i hasMarker...", "Turns on the hasPin... and hasMarker... flags."), actPins},
            {tr("Wszystkie stacje szybkiej podróży", "All fast travel stations"),
             tr("Włącza flagi Unlocked... (bez slotów i melodii) oraz pojawienie się stworzenia dzwonu.",
                "Turns on the Unlocked... flags (except slots and melodies) and the bell beast's appearance."),
             actFastTravel},
            {tr("Uratuj wszystkie pchły", "Rescue all fleas"),
             tr("Włącza wszystkie flagi SavedFlea_...", "Turns on every SavedFlea_... flag."), actFleas},
            {tr("Dziennik łowcy: wszystkie wpisy widziane", "Hunter's Journal: mark every entry as seen"),
             tr("Oznacza każdy wpis dziennika jako widziany (z co najmniej 1 pokonanym). Nie dodaje nowych wpisów.",
                "Marks every journal entry as seen (with at least 1 kill). Does not add new entries."),
             actJournalSeen},
        };
    }
    return actions;
}

std::string summary(json::Value& root) {
    json::Value* p = playerData(root);
    if (!p) return "";
    std::string s;
    auto add = [&](const char* key, const char* label) {
        json::Value* v = p->find(key);
        if (!v || v->type != json::Type::Number) return;
        if (!s.empty()) s += "  ·  ";
        s += label;
        s += " ";
        s += v->text;
    };
    add("geo", tr("Różańce", "Rosaries"));
    add("maxHealth", tr("Maski", "Masks"));
    add("silkMax", tr("Jedwab", "Silk"));
    add("ShellShards", tr("Odłamki", "Shards"));
    if (json::Value* t = p->find("playTime")) {
        const double secs = t->type == json::Type::Number ? json::asDouble(*t) : -1.0;
        if (secs >= 0.0 && secs < 1e12) {
            const long long sec = static_cast<long long>(secs);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s %lld h %02lld min", tr("Czas", "Time"), sec / 3600, (sec / 60) % 60);
            if (!s.empty()) s += "  ·  ";
            s += buf;
        }
    }
    return s;
}

bool loadDefinitions(const std::vector<uint8_t>& data, std::string& err, int& added) {
    added = 0;
    json::Value root;
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(reinterpret_cast<const char*>(data.data()), data.size(), root, end, pe) ||
        root.type != json::Type::Object) {
        err = tr("definitions.json: błędny JSON (", "definitions.json: invalid JSON (") + pe.message + ")";
        return false;
    }
    Registry& r = reg();
    auto str = [](const json::Value* v) { return (v && v->type == json::Type::String) ? v->text : std::string(); };
    if (const json::Value* tools = root.find("tools")) {
        for (const json::Value& t : tools->items) {
            const std::string name = str(t.find("name"));
            if (name.empty()) continue;
            const json::Value* amount = t.find("amount");
            const json::Value* hidden = t.find("hidden");
            const std::string display = str(t.find("display"));
            if (!display.empty()) r.names[name] = display;
            bool known = false;
            for (const ToolDef& d : r.tools) known = known || d.name == name;
            if (known) continue;
            r.tools.push_back({name, hidden && hidden->type == json::Type::Bool && hidden->boolean,
                               amount && json::isIntegerNumber(*amount) ? static_cast<int>(json::asInt(*amount)) : 0});
            ++added;
        }
    }
    if (const json::Value* crests = root.find("crests")) {
        for (const json::Value& c : crests->items) {
            const std::string name = str(c.find("name"));
            const json::Value* slots = c.find("slots");
            if (name.empty() || !slots || !json::isIntegerNumber(*slots)) continue;
            const std::string display = str(c.find("display"));
            if (!display.empty()) r.names[name] = display;
            bool known = false;
            for (const CrestDef& d : r.crests) known = known || d.name == name;
            if (known) continue;
            r.crests.push_back({name, static_cast<int>(std::min<long long>(16, std::max<long long>(0, json::asInt(*slots)))), true});
            ++added;
        }
    }
    if (const json::Value* bosses = root.find("bosses")) {
        for (const json::Value& b : bosses->items) {
            const std::string key = str(b.find("key"));
            if (key.empty()) continue;
            std::string label = str(b.find("label"));
            if (label.empty()) label = key;
            bool known = false;
            for (const BossDef& d : r.bosses) known = known || d.key == key;
            if (known) continue;
            r.bosses.push_back({label, label, key});
            ++added;
        }
    }
    if (const json::Value* names = root.find("names")) {
        for (const json::Member& m : names->members) {
            if (m.value.type != json::Type::String || m.value.text.empty()) continue;
            r.names[m.key] = m.value.text;
            ++added;
        }
    }
    return true;
}

SlotPreview previewSave(const std::vector<uint8_t>& file) {
    SlotPreview p;
    SaveCodecInfo info;
    json::Value root;
    std::string err;
    if (!loadSave(file, info, root, err)) return p;
    json::Value* pd = playerData(root);
    if (!pd) return p;
    p.ok = true;
    auto num = [pd](const char* key, long long& out) {
        json::Value* v = pd->find(key);
        if (v && json::isIntegerNumber(*v)) out = json::asInt(*v);
    };
    if (json::Value* t = pd->find("playTime"))
        if (t->type == json::Type::Number && json::isFiniteNumber(*t)) p.playTime = json::asDouble(*t);
    num("completionPercentage", p.completion);
    num("maxHealth", p.maxHealth);
    num("silkMax", p.silkMax);
    num("geo", p.geo);
    num("ShellShards", p.shards);
    num("permadeathMode", p.permadeath);
    if (json::Value* v = pd->find("version"))
        if (v->type == json::Type::String) p.version = v->text;
    if (json::Value* v = pd->find("respawnScene"))
        if (v->type == json::Type::String) p.scene = v->text;
    return p;
}

} // namespace silk
