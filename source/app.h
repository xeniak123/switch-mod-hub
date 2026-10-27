// Application state machine: games -> saves / mods -> editor -> save back.
// The class is split over app.cpp (screens, saves, editor), app_mods.cpp (mods, online repo,
// self update) and app_hex.cpp (binary save editor).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gfx.h"
#include "jksv.h"
#include "json.h"
#include "modengine.h"
#include "platform.h"
#include "repo.h"
#include "savecodec.h"
#include "silksong.h"
#include "updater.h"

struct ListState {
    int sel = 0;
    int scroll = 0;
    void clampTo(int count);
    void ensureVisible(int visible);
};

class App {
public:
    App();
    ~App();
    // Loads fonts, settings and scans the console / SD card. Returns false if no usable font was found.
    bool init();
    void update(const plat::Input& in);
    void draw(Canvas& c);
    bool wantsExit() const { return exit_; }
    // False when the previous frame is still up to date (nothing to animate, no input).
    bool needsRedraw() const;

private:
    // Titles = home (tabs: games, online mods), Backups = one game (tabs: saves, mods)
    enum class Screen { Titles, Backups, Files, Editor };
    enum Tab { TabQuick = 0, TabTree = 1, TabActions = 2, TabChanges = 3, TabHex = 4 };
    // Touch targets
    enum HitList { HitNone = -1, HitTitles, HitBackups, HitFiles, HitQuick, HitTree, HitActions, HitChanges, HitModal,
                   HitTab, HitMods, HitRepo, HitHex };

    struct PathSeg {
        bool isIndex = false;
        std::string key;   // object member key (for display / validation)
        size_t index = 0;  // member or element index
    };
    struct TreeLevel {
        PathSeg seg;
        ListState list;
        std::string filter;
    };
    struct Doc {
        jksv::Backup backup;
        jksv::SaveEntry entry;
        SaveCodecInfo codec;
        json::Value root;
        bool silk = false;    // Silksong / Hollow Knight style save (playerData): quick edit and actions
        bool binary = false;  // not JSON: edited as bytes
        std::vector<uint8_t> bytes, orig;  // binary documents
        uint32_t structVersion = 1;
        std::vector<std::string> removed;  // list entries deleted in the tree (readable paths)
        std::string importedFrom;          // content replaced by an imported save (file name)
        std::unique_ptr<json::Value> undoRoot;
        std::vector<std::string> undoRemoved;
        std::string undoImportedFrom;
        std::string undoLabel;
    };
    struct MenuItem {
        std::string label;
        std::string sub;
        bool enabled = true;
        bool danger = false;
        std::function<void()> action;
    };
    struct Modal {
        std::string title;
        std::string text;
        std::vector<MenuItem> items;  // empty = message with OK
        ListState list;
        uint32_t accent = 0;
    };
    struct Toast {
        std::string text;
        uint64_t until = 0;
        uint32_t color = 0;
    };
    struct Repeat {
        uint64_t pressedAt = 0;
        uint64_t lastFire = 0;
    };
    struct Nav {
        bool up = false, down = false, left = false, right = false;
        long long step = 1;  // value step for left/right (accelerates while held)
    };
    struct Change {
        enum Kind { Value, Added, Removed, Imported, Bytes } kind = Value;
        std::string path;  // readable path
        std::string before, after;
        json::Value* node = nullptr;    // Value / Added
        json::Value* parent = nullptr;  // container holding the node
        size_t index = 0;               // index of the node in parent (Bytes: first offset)
        size_t length = 0;              // Bytes: number of bytes
        std::vector<PathSeg> segs;      // path to parent (for "show in tree")
        long long limit = 0;            // game limit exceeded by the new value
    };
    struct Hit {
        int x, y, w, h;
        int list;  // HitList (HitNone: press `buttons` instead)
        int index;
        uint32_t buttons;
    };
    struct ScrollArea {
        int x = 0, y = 0, w = 0, h = 0;
        int list = HitNone;
        int rowH = 1, visible = 1, count = 0;
    };
    struct Icon {
        std::vector<uint8_t> jpeg;
        std::vector<uint32_t> px;  // decoded on first use
        bool tried = false;
    };
    struct ModRow {
        int local = -1;   // index into gameMods_
        int remote = -1;  // index into repo_.mods
    };

    // --- input ---
    Nav readNav(const plat::Input& in);
    static bool fireRepeat(Repeat& r, bool down, bool held, uint64_t now, uint64_t delay, uint64_t interval);
    void moveList(ListState& ls, int count, const Nav& nav, const plat::Input& in, int visible);
    void updateBrowser(const plat::Input& in, const Nav& nav);
    void updateEditor(const plat::Input& in, const Nav& nav);
    void updateQuick(const plat::Input& in, const Nav& nav);
    void updateTree(const plat::Input& in, const Nav& nav);
    void updateActions(const plat::Input& in, const Nav& nav);
    void updateChanges(const plat::Input& in, const Nav& nav);
    void updateModal(const plat::Input& in, const Nav& nav);
    void handleTouch(plat::Input& in);
    void tapAt(int x, int y, plat::Input& in);
    void scrollArea(const ScrollArea& a, int steps);
    ListState* listFor(int list);
    void selectTab(int index);

    // --- data ---
    void loadTitles();
    void loadGames();
    std::string gameName(uint64_t titleId) const;
    void openTitle(const jksv::TitleDir& t);
    void reloadBackups();
    void openBackup(const jksv::Backup& b);
    bool loadFiles(const jksv::Backup& b);
    void loadDoc(const jksv::Backup& b, const jksv::SaveEntry& e);
    void closeEditor();
    std::string titleLabel(const jksv::TitleDir& t) const;
    std::string backupLabel(const jksv::Backup& b) const;
    const jksv::TitleDir* titleById(uint64_t titleId) const;

    // --- editor ops ---
    void markEdited();
    void markStructureChanged();
    const std::vector<Change>& changes();
    void walkChanges(json::Value& v, std::vector<PathSeg>& segs);
    std::string pathLabel(const std::vector<PathSeg>& segs, size_t lastIndex);
    int changeCount();
    bool docDirty();
    std::vector<int> editorTabs() const;
    bool docBytes(std::vector<uint8_t>& out, std::string& err, bool forGame);
    void rebuildQuickRows();
    bool rowModified(const silk::QuickRow& r) const;
    void applyQuickInt(silk::QuickRow& r, long long v);
    void applyQuickFloat(silk::QuickRow& r, double v);
    void applyQuickBool(silk::QuickRow& r, bool b);
    void applyQuickChoice(silk::QuickRow& r, int c);
    void quickAdjust(silk::QuickRow& r, long long delta);
    void quickEdit(silk::QuickRow& r);
    void quickRevert(silk::QuickRow& r);
    void quickNextSection();
    json::Value* resolveTree();
    std::vector<PathSeg> treeSegs() const;
    void rebuildTreeRows(json::Value* container);
    std::string childLabel(const json::Value& container, size_t idx) const;
    static json::Value* treeChild(json::Value* container, size_t idx);
    void treeAdjust(json::Value& v, long long delta);
    void treeEdit(json::Value& v, const std::string& label);
    void treeContextMenu();
    std::string treePathText(bool includeSelected);
    long long treeLimit(const json::Value& container, size_t idx);
    void revertChange(size_t idx);
    void showChangeInTree(size_t idx);
    int actionRowCount();
    const std::vector<int>& actionCounts();
    void runAction(size_t idx);
    void snapshotUndo(const std::string& label);
    void undoAction();
    void importMenu();
    void importFile(const std::string& path);
    void exportSave();
    void openSaveMenu();
    void saveAsNew();
    void saveOverwrite();
    void saveToGame(const jksv::Backup& live);
    void saveLiveAsNew();
    void markSaved();
    void requestLeaveEditor();

    // --- binary (hex) editor: app_hex.cpp ---
    void updateHex(const plat::Input& in, const Nav& nav);
    void drawHexTab(Canvas& c);
    void hexEdit();
    void hexSearch();
    void collectByteChanges();

    // --- mods, online repo, self update: app_mods.cpp ---
    void reloadGameMods();
    void rebuildModRows();
    void updateGameMods(const plat::Input& in, const Nav& nav);
    void drawGameMods(Canvas& c);
    void modMenu(size_t row);
    const repo::RemoteMod* remoteFor(uint64_t titleId, const std::string& id) const;
    void toggleMod(uint64_t titleId, const std::string& id, bool on);
    void installRemote(size_t remoteIndex, uint64_t titleId, bool enableAfter);
    void removeLocalMod(uint64_t titleId, const std::string& id);
    void importModMenu();
    void importModFile(const std::string& path);
    void ensureRepo(bool refresh, std::function<void()> then);
    void loadRepoNow(bool refresh);
    void rebuildRepoRows();
    void updateRepo(const plat::Input& in, const Nav& nav);
    void drawRepo(Canvas& c);
    void repoMenu(size_t row);
    uint64_t titleForRemote(const repo::RemoteMod& m) const;
    void checkForUpdate(bool interactive);
    void downloadAndInstallUpdate(const update::Release& rel);

    // --- browser menu ---
    void openBrowserMenu();

    // --- ui helpers ---
    void showMessage(const std::string& title, const std::string& text, uint32_t accent = 0);
    void showConfirm(const std::string& title, const std::string& text, const std::string& yes,
                     std::function<void()> onYes, bool danger = false);
    void toast(const std::string& text, uint32_t color = 0);
    void runBusy(const std::string& message, std::function<void()> task);
    void addHit(int x, int y, int w, int h, int list, int index, uint32_t buttons = 0);
    void addArea(int x, int y, int w, int h, int list, int rowH, int visible, int count);

    // --- drawing ---
    void renderLayers();
    void drawScreen(Canvas& c);
    uint64_t overlaySignature() const;
    void drawBackground(Canvas& c);
    void drawEmblem(Canvas& c, float cx, float cy, float r);
    void drawHeader(Canvas& c, const std::string& title, const std::string& subtitle);
    void drawSteps(Canvas& c, int active);
    void drawFooter(Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints,
                    const std::string& leftNote = "");
    void drawTabBar(Canvas& c, const std::vector<std::string>& labels, int active);
    int buttonBadgeWidth(const std::string& btn);
    int drawButtonBadge(Canvas& c, int x, int y, const std::string& btn);
    void drawPanel(Canvas& c, int x, int y, int w, int h);
    void drawScrollbar(Canvas& c, int x, int y, int h, int total, int visible, int first);
    void drawRowHighlight(Canvas& c, int x, int y, int w, int h);
    void drawToggle(Canvas& c, int x, int y, bool on);
    void drawArrow(Canvas& c, float cx, float cy, float size, int dir, uint32_t color);
    void drawIconFolder(Canvas& c, int x, int y, uint32_t color);
    void drawIconZip(Canvas& c, int x, int y, uint32_t color);
    void drawIconFile(Canvas& c, int x, int y, uint32_t color);
    void drawIconConsole(Canvas& c, int x, int y);
    void drawIconShield(Canvas& c, int x, int y, uint32_t color);
    void drawIconPuzzle(Canvas& c, int x, int y, uint32_t color);
    void drawGameIcon(Canvas& c, int x, int y, int size, const jksv::TitleDir& t);
    void drawWarnBadge(Canvas& c, int cx, int cy);
    int drawTag(Canvas& c, int x, int y, const std::string& text, uint32_t color);
    void drawEmptyState(Canvas& c, int x, int y, int w, int h, const std::string& title, const std::string& text);
    void drawTitles(Canvas& c);
    void drawGames(Canvas& c);
    void drawBackups(Canvas& c);
    void drawSaves(Canvas& c);
    void drawFiles(Canvas& c);
    void drawEditor(Canvas& c);
    void drawQuickTab(Canvas& c);
    void drawTreeTab(Canvas& c);
    void drawActionsTab(Canvas& c);
    void drawChangesTab(Canvas& c);
    void drawValueDetails(Canvas& c, int x, int y, int w, int h, const std::string& title,
                          const std::string& keyPath, const json::Value* v, const std::string& help,
                          const std::string& controls, const std::string& valueText = "",
                          bool valueModified = false, const std::string& warning = "");
    void drawModal(Canvas& c);
    void drawHelp(Canvas& c);
    void drawBusy(Canvas& c);

    FontSet font_;
    // Pre-rendered static chrome (background, bars, panels) for browser and editor screens.
    std::vector<uint32_t> layerBrowse_;
    std::vector<uint32_t> layerEdit_;
    // Dimmed copy of the screen behind a dialog, so dialogs redraw cheaply.
    std::vector<uint32_t> overlayBase_;
    uint64_t overlaySig_ = 0;
    bool redraw_ = true;
    uint64_t lastDraw_ = 0;
    bool exit_ = false;
    uint64_t now_ = 0;
    Screen screen_ = Screen::Titles;
    int homeTab_ = 0;  // 0 games, 1 online mods
    int gameTab_ = 0;  // 0 saves, 1 mods

    std::string workDir_;
    std::string workDirNote_;
    std::vector<plat::GameInfo> games_;  // installed games (icons moved to icons_)
    std::vector<Icon> icons_;
    uint64_t silksongId_ = 0;
    std::vector<plat::SaveUser> liveUsers_;  // users with save data of the open game
    std::string liveError_;
    std::vector<jksv::TitleDir> titles_;
    ListState titleList_;
    jksv::TitleDir title_;
    std::vector<jksv::Backup> backups_;
    ListState backupList_;
    jksv::Backup backup_;
    std::vector<jksv::SaveEntry> files_;
    std::vector<silk::SlotPreview> previews_;  // parallel to files_
    ListState fileList_;

    mods::Paths modPaths_;
    std::vector<mods::Mod> gameMods_;  // mods of the open game
    std::vector<ModRow> modRows_;
    ListState modList_;
    repo::Index repo_;
    bool repoTried_ = false;
    bool repoFromCache_ = false;
    std::string repoError_;
    std::string repoQuery_;
    bool repoAllGames_ = false;
    std::vector<size_t> repoRows_;
    ListState repoList_;

    std::unique_ptr<Doc> doc_;
    int tab_ = TabQuick;
    std::vector<silk::QuickRow> quickRows_;
    uint32_t quickVersion_ = 0;
    ListState quickList_;
    std::vector<TreeLevel> tree_;
    std::vector<size_t> treeRows_;
    const json::Value* treeRowsFor_ = nullptr;
    uint32_t treeRowsVersion_ = 0;
    uint64_t treeRowsEdit_ = ~0ull;
    std::string treeRowsFilter_;
    ListState actionList_;
    std::vector<int> actionCounts_;
    uint64_t actionCountsEdit_ = ~0ull;
    std::vector<Change> changes_;
    uint64_t changesEdit_ = ~0ull;
    ListState changeList_;
    uint64_t editCounter_ = 1;
    size_t hexCursor_ = 0;
    int hexScroll_ = 0;
    std::string hexQuery_;

    std::vector<Modal> modals_;
    bool helpVisible_ = false;
    std::string busyMessage_;
    std::function<void()> busyTask_;
    int busyFrames_ = 0;
    std::vector<Toast> toasts_;

    Repeat repUp_, repDown_, repLeft_, repRight_;

    std::vector<Hit> hits_;
    std::vector<ScrollArea> areas_;
    bool touchActive_ = false;
    bool touchMoved_ = false;
    int touchX0_ = 0, touchY0_ = 0, touchY_ = 0;
    int dragAccum_ = 0;
    ScrollArea dragArea_;
};
