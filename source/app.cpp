#include "app.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_ui.h"
#include "crypto.h"
#include "fsutil.h"
#include "i18n.h"
#include "image.h"
#include "log.h"
#include "settings.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

namespace ui {

std::string formatSize(uint64_t b) {
    char buf[32];
    if (b < 1024) std::snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned>(b));
    else if (b < 1024 * 1024) std::snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>((b + 1023) / 1024));
    else std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(b) / (1024.0 * 1024.0));
    std::string s = buf;
    if (!i18n::english()) std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string lowerAscii(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

bool containsNoCase(const std::string& hay, const std::string& needleLower) {
    if (needleLower.empty()) return true;
    return lowerAscii(hay).find(needleLower) != std::string::npos;
}

std::string displaySd(const std::string& realPath) {
    // Show SD paths as "sdmc:/..." even when the platform maps them elsewhere (PC tests).
    const std::string root = plat::sdPath("sdmc:/");
    if (!root.empty() && realPath.compare(0, root.size(), root) == 0) return "sdmc:/" + realPath.substr(root.size());
    return realPath;
}

std::string elementsText(size_t n) {
    return i18n::plural(static_cast<long long>(n), "element", "elementy", "elementów", "element", "elements");
}

std::string fieldsText(size_t n) {
    return i18n::plural(static_cast<long long>(n), "pole", "pola", "pól", "field", "fields");
}

std::string typeName(const json::Value& v) {
    switch (v.type) {
        case json::Type::Null: return "null";
        case json::Type::Bool: return tr("Tak / Nie (bool)", "Yes / No (bool)");
        case json::Type::Number:
            return json::isIntegerNumber(v) ? tr("Liczba całkowita", "Integer") : tr("Liczba zmiennoprzecinkowa", "Decimal number");
        case json::Type::String: return tr("Tekst", "Text");
        case json::Type::Array: return tr("Lista (", "List (") + elementsText(v.items.size()) + ")";
        case json::Type::Object: return tr("Obiekt (", "Object (") + fieldsText(v.members.size()) + ")";
    }
    return "";
}

std::string valuePreview(const json::Value& v) {
    switch (v.type) {
        case json::Type::Null: return "null";
        case json::Type::Bool: return v.boolean ? "true" : "false";
        case json::Type::Number: return v.text;
        case json::Type::String: return "\"" + v.text + "\"";
        case json::Type::Array: return elementsText(v.items.size());
        case json::Type::Object: return fieldsText(v.members.size());
    }
    return "";
}

std::string formatDuration(double seconds) {
    if (!(seconds >= 0.0) || seconds > 1e12) return "";
    const long long s = static_cast<long long>(seconds);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld h %02lld min %02lld s", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

std::string formatHours(double seconds) {
    if (!(seconds >= 0.0) || seconds > 1e12) return "";
    const long long s = static_cast<long long>(seconds);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%lld h %02lld min", s / 3600, (s / 60) % 60);
    return buf;
}

bool parseIntText(std::string s, long long& out) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno == ERANGE || end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parseFloatText(std::string s, double& out) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    std::replace(s.begin(), s.end(), ',', '.');
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

long long addClamped(long long a, long long b) {
    if (b > 0 && a > LLONG_MAX - b) return LLONG_MAX;
    if (b < 0 && a < LLONG_MIN - b) return LLONG_MIN;
    return a + b;
}

long long clampLL(long long v, long long lo, long long hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Whether an integer field should be kept in 32-bit range (Unity "int" fields).
bool keepInt32(const json::Value& v) {
    const std::string& t = (v.modified && v.origType == json::Type::Number) ? v.origText : v.text;
    const long long o = std::strtoll(t.c_str(), nullptr, 10);
    return o >= INT32_MIN && o <= INT32_MAX;
}

void revertTree(json::Value& v) {
    if (v.modified) json::revert(v);
    for (json::Value& c : v.items) revertTree(c);
    for (json::Member& m : v.members) revertTree(m.value);
}

std::string elementName(const json::Value& e) {
    if (e.type != json::Type::Object) return "";
    static const char* const kNameKeys[] = {"Name", "name", "Key", "key", "ID", "id", "Id"};
    static const char* const kSceneKeys[] = {"SceneName", "sceneName", "Scene", "scene"};
    std::string name, scene;
    for (const char* k : kNameKeys) {
        const json::Value* v = e.find(k);
        if (v && v->type == json::Type::String && !v->text.empty()) {
            name = v->text;
            break;
        }
    }
    for (const char* k : kSceneKeys) {
        const json::Value* v = e.find(k);
        if (v && v->type == json::Type::String && !v->text.empty()) {
            scene = v->text;
            break;
        }
    }
    if (!name.empty()) {
        const std::string d = silk::displayName(name);
        if (!d.empty()) name = d + " (" + name + ")";
    }
    if (!scene.empty() && !name.empty()) return scene + " · " + name;
    return name.empty() ? scene : name;
}

uint32_t buttonFor(const std::string& badge) {
    static const std::pair<const char*, uint32_t> map[] = {
        {"A", plat::BTN_A},   {"B", plat::BTN_B},   {"X", plat::BTN_X},     {"Y", plat::BTN_Y},
        {"L", plat::BTN_L},   {"R", plat::BTN_R},   {"ZL", plat::BTN_ZL},   {"ZR", plat::BTN_ZR},
        {"+", plat::BTN_PLUS}, {"-", plat::BTN_MINUS},
    };
    for (const auto& m : map)
        if (badge == m.first) return m.second;
    return 0;
}

std::string saveName(const json::Value& v) {
    switch (v.type) {
        case json::Type::Bool: return v.boolean ? "true" : "false";
        case json::Type::String: return "\"" + v.text + "\"";
        default: return valuePreview(v);
    }
}

} // namespace ui

using namespace ui;

// =====================================================================================
// ListState
// =====================================================================================

void ListState::clampTo(int count) {
    if (count <= 0) {
        sel = 0;
        scroll = 0;
        return;
    }
    if (sel >= count) sel = count - 1;
    if (sel < 0) sel = 0;
    if (scroll > sel) scroll = sel;
    if (scroll < 0) scroll = 0;
}

void ListState::ensureVisible(int visible) {
    if (visible < 1) visible = 1;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + visible) scroll = sel - visible + 1;
    if (scroll < 0) scroll = 0;
}

// =====================================================================================
// Setup
// =====================================================================================

App::App() {}
App::~App() {}

bool App::init() {
    now_ = plat::nowMs();
    for (const plat::FontBlob& f : plat::fonts()) font_.addFace(f.data, f.size);
    if (!font_.ready()) return false;
    // settings - on the first start the ones of the old Silksong Save Editor are taken over
    const std::string settingsPath = plat::sdPath(kSettingsSd);
    settings::load(fsu::exists(settingsPath) ? settingsPath : plat::sdPath(kLegacySettingsSd));
    i18n::setLanguage(settings::get().language);
    modPaths_.sdRoot = plat::sdPath("sdmc:/");
    modPaths_.modsDir = plat::sdPath(kModsDirSd);
    for (const char* path : {kDefinitionsSd, "sdmc:/switch/SilksongSaveEditor/definitions.json"}) {
        std::vector<uint8_t> defs;
        std::string err;
        if (!fsu::readFile(plat::sdPath(path), defs, err)) continue;
        int added = 0;
        if (silk::loadDefinitions(defs, err, added)) {
            logf("definitions.json: %d entries", added);
        } else {
            logf("%s", err.c_str());
            toast(err, col::err);
        }
        break;
    }
    renderLayers();
    loadTitles();
    // reading the installed games (names, icons) and the update check take a moment
    runBusy(tr("Wczytywanie gier...", "Loading games..."), [this] {
        loadGames();
        loadTitles();
        if (settings::get().autoUpdate) checkForUpdate(false);
    });
    return true;
}

void App::loadGames() {
    std::vector<plat::GameInfo> list;
    std::string err;
    if (!plat::listGames(list, err)) logf("games: %s", err.c_str());
    games_.clear();
    icons_.clear();
    silksongId_ = 0;
    for (plat::GameInfo& g : list) {
        Icon ic;
        ic.jpeg = std::move(g.iconJpeg);
        g.iconJpeg.clear();
        icons_.push_back(std::move(ic));
        if (!silksongId_ && lowerAscii(g.name).find("silksong") != std::string::npos) silksongId_ = g.titleId;
        games_.push_back(std::move(g));
    }
}

std::string App::gameName(uint64_t titleId) const {
    for (const plat::GameInfo& g : games_)
        if (g.titleId == titleId) return g.name;
    return mods::titleHex(titleId);
}

const jksv::TitleDir* App::titleById(uint64_t titleId) const {
    if (title_.titleId == titleId && titleId) return &title_;
    for (const jksv::TitleDir& t : titles_)
        if (t.titleId == titleId && titleId) return &t;
    return nullptr;
}

void App::loadTitles() {
    workDir_ = jksv::findWorkDir(workDirNote_);
    std::vector<jksv::TitleDir> folders;
    jksv::listTitles(workDir_, folders);
    titles_.clear();
    // installed games, plus games that only have save data or mods
    std::vector<uint64_t> ids;
    for (const plat::GameInfo& g : games_) ids.push_back(g.titleId);
    for (uint64_t t : plat::titlesWithSaves())
        if (std::find(ids.begin(), ids.end(), t) == ids.end()) ids.push_back(t);
    for (uint64_t t : mods::gamesWithMods(modPaths_))
        if (std::find(ids.begin(), ids.end(), t) == ids.end()) ids.push_back(t);
    for (uint64_t tid : ids) {
        jksv::TitleDir t;
        t.titleId = tid;
        t.name = gameName(tid);
        t.exists = false;
        for (size_t i = 0; i < games_.size(); ++i)
            if (games_[i].titleId == tid) t.icon = static_cast<int>(i);
        // the JKSV folder: same name (JKSV drops characters like ':') or the title id (JKSV "Use Title IDs")
        const std::string norm = jksv::normalizedName(t.name);
        for (auto it = folders.begin(); it != folders.end(); ++it) {
            uint64_t ftid = 0;
            if ((!norm.empty() && jksv::normalizedName(it->name) == norm) || (mods::parseTitleHex(it->name, ftid) && ftid == tid)) {
                t.path = it->path;
                t.exists = true;
                t.backupCount = it->backupCount;
                folders.erase(it);
                break;
            }
        }
        std::vector<plat::SaveUser> users;
        std::string err;
        if (plat::liveUsers(tid, users, err)) t.liveCount = static_cast<int>(users.size());
        for (const mods::Mod& m : mods::list(modPaths_, tid)) {
            ++t.modsInstalled;
            if (m.enabled) ++t.modsEnabled;
        }
        t.isSilksong = lowerAscii(t.name).find("silksong") != std::string::npos;
        titles_.push_back(t);
    }
    // JKSV folders of games that are not installed
    for (const jksv::TitleDir& f : folders) titles_.push_back(f);
    std::stable_sort(titles_.begin(), titles_.end(), [](const jksv::TitleDir& a, const jksv::TitleDir& b) {
        return lowerAscii(a.name) < lowerAscii(b.name);
    });
    for (const char* dir : {kSafetyDirSd, kLegacySafetyDirSd}) {
        jksv::TitleDir safety;
        safety.path = plat::sdPath(dir);
        safety.isSafety = true;
        safety.backupCount = jksv::countBackups(safety.path);
        if (safety.backupCount > 0) titles_.push_back(safety);
    }
    titleList_.clampTo(static_cast<int>(titles_.size()));
}

std::string App::titleLabel(const jksv::TitleDir& t) const {
    if (t.isSafety)
        return t.path == plat::sdPath(kLegacySafetyDirSd) ? tr("Stare kopie bezpieczeństwa (Silksong Save Editor)", "Old safety copies (Silksong Save Editor)")
                                                          : tr("Kopie bezpieczeństwa edytora", "Editor safety copies");
    return t.name;
}

std::string App::backupLabel(const jksv::Backup& b) const {
    if (b.isLive) return tr("Zapis w grze – ", "Game save – ") + b.userName;
    return b.name;
}

void App::openTitle(const jksv::TitleDir& t) {
    title_ = t;
    liveUsers_.clear();
    liveError_.clear();
    if (t.titleId && !plat::liveUsers(t.titleId, liveUsers_, liveError_)) liveUsers_.clear();
    backupList_ = ListState();
    modList_ = ListState();
    gameTab_ = 0;
    reloadBackups();
    reloadGameMods();
    screen_ = Screen::Backups;
}

void App::reloadBackups() {
    backups_.clear();
    for (const plat::SaveUser& u : liveUsers_) backups_.push_back(jksv::liveBackup(title_.titleId, u));
    if (title_.exists && !title_.path.empty()) {
        std::vector<jksv::Backup> disk;
        jksv::listBackups(title_.path, disk, title_.isSafety);
        backups_.insert(backups_.end(), disk.begin(), disk.end());
    }
    title_.backupCount = static_cast<int>(backups_.size() - liveUsers_.size());
    backupList_.clampTo(static_cast<int>(backups_.size()));
}

bool App::loadFiles(const jksv::Backup& b) {
    std::vector<jksv::SaveEntry> files;
    std::string err;
    if (!jksv::listSaveFiles(b, files, err)) {
        showMessage(tr("Nie można otworzyć", "Cannot open"), err, col::err);
        logf("list failed: %s", err.c_str());
        return false;
    }
    backup_ = b;
    files_ = files;
    previews_.assign(files_.size(), silk::SlotPreview());
    std::vector<std::string> names;
    std::vector<size_t> where;
    for (size_t i = 0; i < files_.size(); ++i) {
        if (files_[i].slot > 0 && files_[i].size < 32u * 1024u * 1024u) {
            names.push_back(files_[i].name);
            where.push_back(i);
        }
    }
    std::vector<std::vector<uint8_t>> data;
    if (!names.empty() && jksv::readSaveFiles(b, names, data, err)) {
        for (size_t k = 0; k < where.size(); ++k) previews_[where[k]] = silk::previewSave(data[k]);
    }
    fileList_.clampTo(static_cast<int>(files_.size()));
    return true;
}

void App::openBackup(const jksv::Backup& b) {
    runBusy(b.isLive ? tr("Wczytywanie zapisu gry...", "Loading the game save...") : tr("Wczytywanie kopii...", "Loading the backup..."),
            [this, b] {
                if (loadFiles(b)) {
                    fileList_ = ListState();
                    screen_ = Screen::Files;
                }
            });
}

void App::loadDoc(const jksv::Backup& b, const jksv::SaveEntry& e) {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!jksv::readSaveFile(b, e.name, bytes, err)) {
        showMessage(tr("Błąd odczytu", "Read error"), err, col::err);
        logf("read failed: %s", err.c_str());
        return;
    }
    std::unique_ptr<Doc> d(new Doc());
    d->backup = b;
    d->entry = e;
    if (loadSave(bytes, d->codec, d->root, err)) {
        const json::Value* pd = d->root.find("playerData");
        d->silk = pd && pd->type == json::Type::Object;
    } else {
        // a format the editor does not understand: edit the raw bytes
        d->binary = true;
        d->bytes = bytes;
        d->orig = bytes;
        d->root = json::Value();
    }
    logf("opened %s / %s (%s, %u bytes)", b.isLive ? ("game save " + b.userName).c_str() : b.name.c_str(), e.name.c_str(),
         d->binary ? "binary" : saveFormatName(d->codec.format), static_cast<unsigned>(bytes.size()));
    doc_ = std::move(d);
    tab_ = doc_->binary ? TabHex : (doc_->silk ? TabQuick : TabTree);
    quickRows_.clear();
    quickVersion_ = 0;
    quickList_ = ListState();
    tree_.clear();
    tree_.push_back(TreeLevel());
    treeRows_.clear();
    treeRowsFor_ = nullptr;
    actionList_ = ListState();
    actionCounts_.clear();
    changeList_ = ListState();
    hexCursor_ = 0;
    hexScroll_ = 0;
    ++editCounter_;
    screen_ = Screen::Editor;
    if (doc_->binary) toast(tr("Plik binarny – edytor HEX", "Binary file - hex editor"));
}

void App::closeEditor() {
    doc_.reset();
    quickRows_.clear();
    tree_.clear();
    treeRows_.clear();
    treeRowsFor_ = nullptr;
    actionCounts_.clear();
    changes_.clear();
    ++editCounter_;
    screen_ = Screen::Files;
    // the backup may have changed (save as new / save to the game) - refresh the file list
    const jksv::Backup b = backup_;
    runBusy(tr("Wczytywanie...", "Loading..."), [this, b] {
        const int sel = fileList_.sel;
        loadFiles(b);
        fileList_.sel = sel;
        fileList_.clampTo(static_cast<int>(files_.size()));
    });
}

// =====================================================================================
// Input
// =====================================================================================

bool App::fireRepeat(Repeat& r, bool down, bool held, uint64_t now, uint64_t delay, uint64_t interval) {
    if (down) {
        r.pressedAt = now;
        r.lastFire = now;
        return true;
    }
    if (!held) {
        r.pressedAt = 0;
        return false;
    }
    if (r.pressedAt && now - r.pressedAt >= delay && now - r.lastFire >= interval) {
        r.lastFire = now;
        return true;
    }
    return false;
}

App::Nav App::readNav(const plat::Input& in) {
    Nav n;
    n.up = fireRepeat(repUp_, in.down & plat::BTN_UP, in.held & plat::BTN_UP, now_, 360, 55);
    n.down = fireRepeat(repDown_, in.down & plat::BTN_DOWN, in.held & plat::BTN_DOWN, now_, 360, 55);
    n.left = fireRepeat(repLeft_, in.down & plat::BTN_LEFT, in.held & plat::BTN_LEFT, now_, 360, 65);
    n.right = fireRepeat(repRight_, in.down & plat::BTN_RIGHT, in.held & plat::BTN_RIGHT, now_, 360, 65);
    uint64_t held = 0;
    if ((in.held & plat::BTN_LEFT) && repLeft_.pressedAt) held = now_ - repLeft_.pressedAt;
    if ((in.held & plat::BTN_RIGHT) && repRight_.pressedAt) held = now_ - repRight_.pressedAt;
    n.step = held > 4500 ? 1000 : held > 3000 ? 100 : held > 1500 ? 10 : 1;
    return n;
}

void App::moveList(ListState& ls, int count, const Nav& nav, const plat::Input& in, int visible) {
    if (count <= 0) {
        ls = ListState();
        return;
    }
    if (nav.up) {
        if (ls.sel > 0) --ls.sel;
        else if (in.down & plat::BTN_UP) ls.sel = count - 1;
    }
    if (nav.down) {
        if (ls.sel < count - 1) ++ls.sel;
        else if (in.down & plat::BTN_DOWN) ls.sel = 0;
    }
    if (in.down & plat::BTN_ZL) ls.sel -= std::max(1, visible - 1);
    if (in.down & plat::BTN_ZR) ls.sel += std::max(1, visible - 1);
    ls.clampTo(count);
    ls.ensureVisible(visible);
}

bool App::needsRedraw() const {
    return redraw_ || busyTask_ || touchActive_ || !toasts_.empty() || now_ - lastDraw_ >= 1000;
}

void App::update(const plat::Input& rawIn) {
    now_ = plat::nowMs();
    const size_t toastCount = toasts_.size();
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [this](const Toast& t) { return t.until < now_; }),
                  toasts_.end());
    plat::Input in = rawIn;
    if (toasts_.size() != toastCount || in.down || in.held || in.touching) redraw_ = true;
    handleTouch(in);

    if (busyTask_) {
        // run the task once its "please wait" overlay is on screen
        if (busyFrames_ < 2) return;
        std::function<void()> task = std::move(busyTask_);
        busyTask_ = nullptr;
        busyFrames_ = 0;
        task();
        busyMessage_.clear();
        redraw_ = true;
        // input of this frame goes to the new screen unless the task opened a dialog or another task
        if (busyTask_ || !modals_.empty()) return;
    }

    const Nav nav = readNav(in);
    if (!modals_.empty()) {
        updateModal(in, nav);
        return;
    }
    if (helpVisible_) {
        if (in.down & (plat::BTN_A | plat::BTN_B | plat::BTN_MINUS | plat::BTN_PLUS)) helpVisible_ = false;
        return;
    }
    if (in.down & plat::BTN_MINUS) {
        helpVisible_ = true;
        return;
    }
    if (screen_ == Screen::Editor && doc_) updateEditor(in, nav);
    else updateBrowser(in, nav);
}

// ---- touch screen: tap = select (tap the selected row again = A), drag = scroll ----

ListState* App::listFor(int list) {
    switch (list) {
        case HitTitles: return &titleList_;
        case HitBackups: return &backupList_;
        case HitFiles: return &fileList_;
        case HitQuick: return &quickList_;
        case HitTree: return tree_.empty() ? nullptr : &tree_.back().list;
        case HitActions: return &actionList_;
        case HitChanges: return &changeList_;
        case HitMods: return &modList_;
        case HitRepo: return &repoList_;
        case HitModal: return modals_.empty() ? nullptr : &modals_.back().list;
        default: return nullptr;
    }
}

void App::handleTouch(plat::Input& in) {
    if (in.touching) {
        if (!touchActive_) {
            touchActive_ = true;
            touchMoved_ = false;
            touchX0_ = in.touchX;
            touchY0_ = touchY_ = in.touchY;
            dragAccum_ = 0;
            dragArea_ = ScrollArea();
            for (size_t i = areas_.size(); i-- > 0;) {
                const ScrollArea& a = areas_[i];
                if (in.touchX >= a.x && in.touchY >= a.y && in.touchX < a.x + a.w && in.touchY < a.y + a.h) {
                    dragArea_ = a;
                    break;
                }
            }
            return;
        }
        if (!touchMoved_ && (std::abs(in.touchX - touchX0_) > 16 || std::abs(in.touchY - touchY0_) > 16))
            touchMoved_ = true;
        const int dy = in.touchY - touchY_;
        touchY_ = in.touchY;
        if (touchMoved_ && dragArea_.list != HitNone) {
            dragAccum_ += dy;
            int steps = 0;
            while (dragAccum_ >= dragArea_.rowH) {
                dragAccum_ -= dragArea_.rowH;
                --steps;  // finger moves down -> show earlier rows
            }
            while (dragAccum_ <= -dragArea_.rowH) {
                dragAccum_ += dragArea_.rowH;
                ++steps;
            }
            if (steps) scrollArea(dragArea_, steps);
        }
        return;
    }
    if (touchActive_) {
        touchActive_ = false;
        redraw_ = true;
        if (!touchMoved_) tapAt(touchX0_, touchY0_, in);
    }
}

void App::scrollArea(const ScrollArea& a, int steps) {
    ListState* ls = listFor(a.list);
    if (!ls) return;
    const int maxScroll = std::max(0, a.count - a.visible);
    ls->scroll = static_cast<int>(clampLL(ls->scroll + steps, 0, maxScroll));
    if (ls->sel < ls->scroll) ls->sel = ls->scroll;
    if (ls->sel >= ls->scroll + a.visible) ls->sel = ls->scroll + a.visible - 1;
    if (a.list == HitQuick) {
        // never rest on a section header
        const int n = static_cast<int>(quickRows_.size());
        auto header = [this](int i) { return quickRows_[static_cast<size_t>(i)].kind == silk::RowKind::Header; };
        int s = ls->sel;
        while (s < n - 1 && s < ls->scroll + a.visible - 1 && header(s)) ++s;
        while (s > 0 && header(s)) --s;
        if (s >= 0 && s < n) ls->sel = s;
    }
    redraw_ = true;
}

void App::tapAt(int x, int y, plat::Input& in) {
    for (size_t i = hits_.size(); i-- > 0;) {
        const Hit h = hits_[i];
        if (x < h.x || y < h.y || x >= h.x + h.w || y >= h.y + h.h) continue;
        if (h.list == HitTab) {
            if (modals_.empty()) selectTab(h.index);
            return;
        }
        if (h.list == HitModal) {
            // menus react immediately
            if (!modals_.empty() && h.index < static_cast<int>(modals_.back().items.size()) &&
                modals_.back().items[static_cast<size_t>(h.index)].enabled) {
                modals_.back().list.sel = h.index;
                in.down |= plat::BTN_A;
                in.held |= plat::BTN_A;
            }
            return;
        }
        if (h.list != HitNone) {
            ListState* ls = listFor(h.list);
            if (!ls) return;
            if (ls->sel == h.index) {
                in.down |= plat::BTN_A;
                in.held |= plat::BTN_A;
            } else {
                ls->sel = h.index;
            }
            return;
        }
        in.down |= h.buttons;
        in.held |= h.buttons;
        return;
    }
}

void App::selectTab(int index) {
    if (screen_ == Screen::Editor && doc_) {
        tab_ = index;
    } else if (screen_ == Screen::Titles) {
        homeTab_ = index == 1 ? 1 : 0;
        if (homeTab_ == 1) ensureRepo(false, [this] { rebuildRepoRows(); });
    } else if (screen_ == Screen::Backups && !title_.isSafety) {
        gameTab_ = index == 1 ? 1 : 0;
        if (gameTab_ == 1) {
            reloadGameMods();
            ensureRepo(false, [this] { rebuildModRows(); });
        }
    }
}

void App::updateBrowser(const plat::Input& in, const Nav& nav) {
    const int tabbedVisible = (kBrowseH - kBrowseTabsH - 16) / kBrowseRowH;
    if ((in.down & plat::BTN_X) && screen_ != Screen::Editor) {
        openBrowserMenu();
        return;
    }
    if ((in.down & (plat::BTN_L | plat::BTN_R)) && (screen_ == Screen::Titles || screen_ == Screen::Backups)) {
        selectTab((screen_ == Screen::Titles ? homeTab_ : gameTab_) == 0 ? 1 : 0);
        return;
    }
    switch (screen_) {
        case Screen::Titles:
            if (homeTab_ == 1) {
                updateRepo(in, nav);
                break;
            }
            moveList(titleList_, static_cast<int>(titles_.size()), nav, in, tabbedVisible);
            if ((in.down & plat::BTN_A) && !titles_.empty()) {
                openTitle(titles_[static_cast<size_t>(titleList_.sel)]);
            } else if (in.down & plat::BTN_Y) {
                runBusy(tr("Wczytywanie gier...", "Loading games..."), [this] {
                    loadGames();
                    loadTitles();
                    toast(tr("Odświeżono listę gier", "Game list refreshed"));
                });
            } else if (in.down & (plat::BTN_PLUS | plat::BTN_B)) {
                exit_ = true;
            }
            break;
        case Screen::Backups:
            if (gameTab_ == 1 && !title_.isSafety) {
                updateGameMods(in, nav);
                break;
            }
            moveList(backupList_, static_cast<int>(backups_.size()), nav, in, tabbedVisible);
            if ((in.down & plat::BTN_A) && !backups_.empty()) {
                openBackup(backups_[static_cast<size_t>(backupList_.sel)]);
            } else if (in.down & plat::BTN_B) {
                loadTitles();
                screen_ = Screen::Titles;
            } else if (in.down & plat::BTN_Y) {
                reloadBackups();
                toast(tr("Odświeżono listę kopii", "Backup list refreshed"));
            } else if (in.down & plat::BTN_PLUS) {
                exit_ = true;
            }
            break;
        case Screen::Files: {
            const int visible = (kBrowseH - 16) / kBrowseRowH;
            moveList(fileList_, static_cast<int>(files_.size()), nav, in, visible);
            if ((in.down & plat::BTN_A) && !files_.empty()) {
                const jksv::SaveEntry e = files_[static_cast<size_t>(fileList_.sel)];
                const jksv::Backup b = backup_;
                runBusy(tr("Wczytywanie ", "Loading ") + e.name + "...", [this, b, e] { loadDoc(b, e); });
            } else if (in.down & plat::BTN_B) {
                reloadBackups();
                screen_ = Screen::Backups;
            } else if (in.down & plat::BTN_PLUS) {
                exit_ = true;
            }
            break;
        }
        case Screen::Editor: break;
    }
}

std::vector<int> App::editorTabs() const {
    if (doc_ && doc_->binary) return {TabHex, TabChanges};
    if (doc_ && doc_->silk) return {TabQuick, TabTree, TabActions, TabChanges};
    return {TabTree, TabChanges};
}

void App::updateEditor(const plat::Input& in, const Nav& nav) {
    if (in.down & (plat::BTN_L | plat::BTN_R)) {
        const std::vector<int> tabs = editorTabs();
        size_t pos = 0;
        for (size_t i = 0; i < tabs.size(); ++i)
            if (tabs[i] == tab_) pos = i;
        pos = (in.down & plat::BTN_L) ? (pos + tabs.size() - 1) % tabs.size() : (pos + 1) % tabs.size();
        tab_ = tabs[pos];
        return;
    }
    if (in.down & plat::BTN_PLUS) {
        openSaveMenu();
        return;
    }
    switch (tab_) {
        case TabQuick: updateQuick(in, nav); break;
        case TabTree: updateTree(in, nav); break;
        case TabActions: updateActions(in, nav); break;
        case TabHex: updateHex(in, nav); break;
        default: updateChanges(in, nav); break;
    }
}

void App::updateModal(const plat::Input& in, const Nav& nav) {
    Modal& m = modals_.back();
    if (m.items.empty()) {
        if (in.down & (plat::BTN_A | plat::BTN_B | plat::BTN_PLUS)) modals_.pop_back();
        return;
    }
    const int n = static_cast<int>(m.items.size());
    auto step = [&](int dir) {
        for (int k = 0; k < n; ++k) {
            m.list.sel = (m.list.sel + dir + n) % n;
            if (m.items[static_cast<size_t>(m.list.sel)].enabled) break;
        }
    };
    if (nav.up) step(-1);
    if (nav.down) step(1);
    if (in.down & plat::BTN_A) {
        const MenuItem item = m.items[static_cast<size_t>(m.list.sel)];
        if (!item.enabled) return;
        modals_.pop_back();
        if (item.action) item.action();
    } else if (in.down & plat::BTN_B) {
        modals_.pop_back();
    }
}

// =====================================================================================
// Editor - shared
// =====================================================================================

void App::markEdited() { ++editCounter_; }

void App::markStructureChanged() {
    if (!doc_) return;
    ++doc_->structVersion;
    ++editCounter_;
}

std::string App::pathLabel(const std::vector<PathSeg>& segs, size_t lastIndex) {
    std::string p;
    json::Value* cur = &doc_->root;
    for (const PathSeg& s : segs) {
        if (!p.empty()) p += " › ";
        p += childLabel(*cur, s.index);
        json::Value* next = treeChild(cur, s.index);
        if (!next) return p;
        cur = next;
    }
    if (!p.empty()) p += " › ";
    return p + childLabel(*cur, lastIndex);
}

void App::walkChanges(json::Value& v, std::vector<PathSeg>& segs) {
    const size_t n = v.childCount();
    for (size_t i = 0; i < n; ++i) {
        json::Value* child = treeChild(&v, i);
        if (!child) continue;
        if (child->added || (child->isScalar() && child->modified)) {
            Change c;
            c.kind = child->added ? Change::Added : Change::Value;
            c.node = child;
            c.parent = &v;
            c.index = i;
            c.segs = segs;
            c.path = pathLabel(segs, i);
            if (child->added) {
                const std::string name = elementName(*child);
                c.after = tr("nowy wpis", "new entry") + (name.empty() ? std::string() : ": " + name);
            } else {
                c.before = json::originalDisplay(*child);
                c.after = saveName(*child);
                if (segs.size() == 1 && !segs[0].isIndex && segs[0].key == "playerData" && v.type == json::Type::Object &&
                    json::isIntegerNumber(*child)) {
                    const long long lim = silk::gameLimit(v.members[i].key);
                    if (lim > 0 && json::asInt(*child) > lim) c.limit = lim;
                }
            }
            changes_.push_back(c);
            continue;
        }
        if (child->isScalar()) continue;
        PathSeg s;
        s.isIndex = v.type == json::Type::Array;
        s.index = i;
        if (!s.isIndex) s.key = v.members[i].key;
        segs.push_back(s);
        walkChanges(*child, segs);
        segs.pop_back();
    }
}

const std::vector<App::Change>& App::changes() {
    if (changesEdit_ != editCounter_) {
        changes_.clear();
        if (doc_ && doc_->binary) {
            collectByteChanges();
        } else if (doc_) {
            if (!doc_->importedFrom.empty()) {
                Change c;
                c.kind = Change::Imported;
                c.path = tr("Cały plik zastąpiony zaimportowanym zapisem", "Whole file replaced by an imported save");
                c.after = doc_->importedFrom;
                changes_.push_back(c);
            }
            std::vector<PathSeg> segs;
            walkChanges(doc_->root, segs);
            for (const std::string& r : doc_->removed) {
                Change c;
                c.kind = Change::Removed;
                c.path = r;
                c.after = tr("usunięty wpis", "removed entry");
                changes_.push_back(c);
            }
        }
        changesEdit_ = editCounter_;
        changeList_.clampTo(static_cast<int>(changes_.size()));
    }
    return changes_;
}

int App::changeCount() { return static_cast<int>(changes().size()); }

bool App::docDirty() { return doc_ && changeCount() > 0; }

void App::requestLeaveEditor() {
    if (docDirty()) {
        showConfirm(tr("Niezapisane zmiany", "Unsaved changes"),
                    tr("Masz niezapisane zmiany w tym zapisie. Wyjść bez zapisywania?",
                       "You have unsaved changes in this save. Leave without saving?"),
                    tr("Wyjdź bez zapisywania", "Leave without saving"), [this] { closeEditor(); }, true);
    } else {
        closeEditor();
    }
}

// =====================================================================================
// Editor - quick tab
// =====================================================================================

void App::rebuildQuickRows() {
    silk::buildQuickRows(doc_->root, quickRows_);
    quickVersion_ = doc_->structVersion;
    const int n = static_cast<int>(quickRows_.size());
    quickList_.clampTo(n);
    // never rest on a section header
    if (n > 0 && quickRows_[static_cast<size_t>(quickList_.sel)].kind == silk::RowKind::Header) {
        int s = quickList_.sel;
        while (s < n && quickRows_[static_cast<size_t>(s)].kind == silk::RowKind::Header) ++s;
        if (s >= n) {
            s = quickList_.sel;
            while (s > 0 && quickRows_[static_cast<size_t>(s)].kind == silk::RowKind::Header) --s;
        }
        quickList_.sel = s;
    }
}

bool App::rowModified(const silk::QuickRow& r) const {
    for (json::Value* v : silk::rowValues(r))
        if (v->modified) return true;
    return false;
}

void App::applyQuickInt(silk::QuickRow& r, long long v) {
    v = clampLL(v, r.minValue, r.maxValue);
    if (r.kind == silk::RowKind::Float) {
        json::setFloat(*r.value, static_cast<double>(v));
    } else {
        json::setInt(*r.value, v);
        for (json::Value* l : r.linked) json::setInt(*l, v);
        for (auto& p : r.whenPositive) {
            if (v > 0) json::setBool(*p.first, p.second);
            else json::revert(*p.first);
        }
    }
    markEdited();
}

void App::applyQuickFloat(silk::QuickRow& r, double v) {
    if (v < static_cast<double>(r.minValue)) v = static_cast<double>(r.minValue);
    if (v > static_cast<double>(r.maxValue)) v = static_cast<double>(r.maxValue);
    json::setFloat(*r.value, v);
    markEdited();
}

void App::applyQuickBool(silk::QuickRow& r, bool b) {
    if (r.value->type != json::Type::Bool || r.value->boolean == b) return;
    json::setBool(*r.value, b);
    if (b)
        for (auto& p : r.whenOn) json::setBool(*p.first, p.second);
    for (auto& p : r.whenChanged) json::setBool(*p.first, p.second);
    markEdited();
}

void App::applyQuickChoice(silk::QuickRow& r, int c) {
    if (!r.setChoice || r.choices.empty()) return;
    const int n = static_cast<int>(r.choices.size());
    c = (c % n + n) % n;
    r.setChoice(c);
    markEdited();
}

void App::quickAdjust(silk::QuickRow& r, long long delta) {
    if (r.kind == silk::RowKind::Float) {
        applyQuickFloat(r, json::asDouble(*r.value) + static_cast<double>(delta));
    } else if (r.kind == silk::RowKind::Int) {
        applyQuickInt(r, addClamped(json::asInt(*r.value), delta));
    }
}

void App::quickEdit(silk::QuickRow& r) {
    if (r.kind == silk::RowKind::Bool) {
        applyQuickBool(r, !r.value->boolean);
        return;
    }
    if (r.kind == silk::RowKind::Choice) {
        applyQuickChoice(r, r.getChoice() + 1);
        return;
    }
    plat::TextRequest req;
    req.header = r.label;
    req.guide = r.kind == silk::RowKind::Float ? tr("Wpisz liczbę (np. 12,5)", "Enter a number (e.g. 12.5)")
                                               : tr("Wpisz liczbę całkowitą", "Enter a whole number");
    req.initial = r.value->text;
    req.numeric = true;
    req.allowNegative = r.minValue < 0;
    req.allowDecimal = r.kind == silk::RowKind::Float;
    req.maxLength = 16;
    std::string out;
    if (!plat::textInput(req, out)) return;
    if (r.kind == silk::RowKind::Float) {
        double d = 0;
        if (!parseFloatText(out, d)) {
            toast(tr("To nie jest poprawna liczba", "That is not a valid number"), col::err);
            return;
        }
        applyQuickFloat(r, d);
    } else {
        long long v = 0;
        if (!parseIntText(out, v)) {
            toast(tr("To nie jest poprawna liczba całkowita", "That is not a valid whole number"), col::err);
            return;
        }
        if (v < r.minValue || v > r.maxValue)
            toast(tr("Wartość ograniczono do zakresu ", "Value limited to ") + std::to_string(r.minValue) + "–" +
                      std::to_string(r.maxValue),
                  col::warn);
        applyQuickInt(r, v);
        if (r.gameMax > 0 && json::asInt(*r.value) > r.gameMax)
            toast(tr("Uwaga: gra ma maksymalnie ", "Note: the game has at most ") + std::to_string(r.gameMax), col::warn);
    }
}

void App::quickRevert(silk::QuickRow& r) {
    if (!rowModified(r)) return;
    for (json::Value* v : silk::rowValues(r)) json::revert(*v);
    markEdited();
    toast(tr("Przywrócono oryginalną wartość", "Original value restored"));
}

void App::quickNextSection() {
    const int n = static_cast<int>(quickRows_.size());
    auto header = [this](int i) { return quickRows_[static_cast<size_t>(i)].kind == silk::RowKind::Header; };
    int h = quickList_.sel + 1;
    while (h < n && !header(h)) ++h;
    if (h >= n) {
        h = 0;
        while (h < n && !header(h)) ++h;
    }
    if (h >= n) return;
    int first = h + 1;
    while (first < n && header(first)) ++first;
    if (first >= n) return;
    quickList_.sel = first;
    quickList_.scroll = h;
    toast(quickRows_[static_cast<size_t>(h)].label);
}

void App::updateQuick(const plat::Input& in, const Nav& nav) {
    if (quickVersion_ != doc_->structVersion) rebuildQuickRows();
    const int n = static_cast<int>(quickRows_.size());
    const int visible = (kEditH - 16) / kQuickRowH;
    if (n == 0) {
        if (in.down & plat::BTN_B) requestLeaveEditor();
        return;
    }
    if (in.down & plat::BTN_Y) {
        quickNextSection();
        return;
    }
    auto isHeader = [this](int i) { return quickRows_[static_cast<size_t>(i)].kind == silk::RowKind::Header; };
    moveList(quickList_, n, nav, in, visible);
    // skip section headers in the direction of travel
    if (isHeader(quickList_.sel)) {
        const int dir = (nav.up || (in.down & plat::BTN_ZL)) ? -1 : 1;
        int s = quickList_.sel;
        while (s >= 0 && s < n && isHeader(s)) s += dir;
        if (s < 0 && (in.down & plat::BTN_UP)) {
            // pressing up on the first item wraps to the last one
            s = n - 1;
            while (s >= 0 && isHeader(s)) --s;
        }
        if (s < 0 || s >= n) {
            s = quickList_.sel;
            while (s >= 0 && s < n && isHeader(s)) s -= dir;
        }
        if (s >= 0 && s < n) quickList_.sel = s;
        quickList_.ensureVisible(visible);
    }
    // keep the section title visible above the first row of a section
    if (quickList_.sel > 0 && quickList_.scroll == quickList_.sel && isHeader(quickList_.sel - 1))
        quickList_.scroll = quickList_.sel - 1;

    silk::QuickRow& r = quickRows_[static_cast<size_t>(quickList_.sel)];
    if (r.kind == silk::RowKind::Header) {
        if (in.down & plat::BTN_B) requestLeaveEditor();
        return;
    }
    if (r.kind == silk::RowKind::Choice) {
        // one step per press: holding would race through the states
        if (in.down & plat::BTN_LEFT) applyQuickChoice(r, r.getChoice() < 0 ? -1 : r.getChoice() - 1);
        if (in.down & plat::BTN_RIGHT) applyQuickChoice(r, r.getChoice() + 1);
    } else if (nav.left || nav.right) {
        if (r.kind == silk::RowKind::Bool) applyQuickBool(r, nav.right);
        else quickAdjust(r, nav.right ? nav.step : -nav.step);
    }
    if (in.down & plat::BTN_A) quickEdit(r);
    else if (in.down & plat::BTN_X) quickRevert(r);
    else if (in.down & plat::BTN_B) requestLeaveEditor();
}

// =====================================================================================
// Editor - tree tab
// =====================================================================================

json::Value* App::treeChild(json::Value* container, size_t idx) {
    if (!container) return nullptr;
    if (container->type == json::Type::Array) return idx < container->items.size() ? &container->items[idx] : nullptr;
    if (container->type == json::Type::Object)
        return idx < container->members.size() ? &container->members[idx].value : nullptr;
    return nullptr;
}

json::Value* App::resolveTree() {
    if (tree_.empty()) tree_.push_back(TreeLevel());
    json::Value* cur = &doc_->root;
    for (size_t i = 1; i < tree_.size(); ++i) {
        const PathSeg& s = tree_[i].seg;
        json::Value* next = nullptr;
        if (s.isIndex && cur->type == json::Type::Array) {
            if (s.index < cur->items.size()) next = &cur->items[s.index];
        } else if (!s.isIndex && cur->type == json::Type::Object) {
            if (s.index < cur->members.size() && cur->members[s.index].key == s.key)
                next = &cur->members[s.index].value;
            else
                next = cur->find(s.key);
        }
        if (!next || !next->isContainer()) {
            tree_.resize(i);
            break;
        }
        cur = next;
    }
    return cur;
}

std::vector<App::PathSeg> App::treeSegs() const {
    std::vector<PathSeg> segs;
    for (size_t i = 1; i < tree_.size(); ++i) segs.push_back(tree_[i].seg);
    return segs;
}

std::string App::childLabel(const json::Value& container, size_t idx) const {
    if (container.type == json::Type::Object) return idx < container.members.size() ? container.members[idx].key : "";
    if (container.type == json::Type::Array && idx < container.items.size()) {
        const std::string name = elementName(container.items[idx]);
        return "[" + std::to_string(idx) + "]" + (name.empty() ? "" : "  " + name);
    }
    return "";
}

void App::rebuildTreeRows(json::Value* container) {
    const std::string& filter = tree_.back().filter;
    if (treeRowsFor_ == container && treeRowsVersion_ == doc_->structVersion && treeRowsEdit_ == editCounter_ &&
        treeRowsFilter_ == filter)
        return;
    treeRows_.clear();
    const std::string f = lowerAscii(filter);
    const size_t n = container->childCount();
    for (size_t i = 0; i < n; ++i) {
        if (!f.empty()) {
            const json::Value* v = treeChild(container, i);
            const bool match = containsNoCase(childLabel(*container, i), f) ||
                               (v->isScalar() && containsNoCase(v->text, f)) ||
                               (v->type == json::Type::Bool && containsNoCase(v->boolean ? "true" : "false", f));
            if (!match) continue;
        }
        treeRows_.push_back(i);
    }
    treeRowsFor_ = container;
    treeRowsVersion_ = doc_->structVersion;
    treeRowsEdit_ = editCounter_;
    treeRowsFilter_ = filter;
    tree_.back().list.clampTo(static_cast<int>(treeRows_.size()));
}

std::string App::treePathText(bool includeSelected) {
    std::string p = "root";
    for (size_t i = 1; i < tree_.size(); ++i) {
        const PathSeg& s = tree_[i].seg;
        p += s.isIndex ? "[" + std::to_string(s.index) + "]" : " › " + s.key;
    }
    if (includeSelected && !treeRows_.empty()) {
        json::Value* cont = resolveTree();
        const size_t idx = treeRows_[static_cast<size_t>(tree_.back().list.sel)];
        if (cont->type == json::Type::Array) p += "[" + std::to_string(idx) + "]";
        else if (idx < cont->members.size()) p += " › " + cont->members[idx].key;
    }
    return p;
}

long long App::treeLimit(const json::Value& container, size_t idx) {
    if (tree_.size() != 2 || tree_[1].seg.isIndex || tree_[1].seg.key != "playerData") return 0;
    if (container.type != json::Type::Object || idx >= container.members.size()) return 0;
    const json::Value& v = container.members[idx].value;
    if (!json::isIntegerNumber(v)) return 0;
    const long long lim = silk::gameLimit(container.members[idx].key);
    return (lim > 0 && json::asInt(v) > lim) ? lim : 0;
}

void App::treeAdjust(json::Value& v, long long delta) {
    if (v.type == json::Type::Bool) {
        if (v.boolean != (delta > 0)) {
            json::setBool(v, delta > 0);
            markEdited();
        }
        return;
    }
    if (v.type != json::Type::Number || !json::isFiniteNumber(v)) return;
    if (json::isIntegerNumber(v)) {
        long long nv = addClamped(json::asInt(v), delta);
        if (keepInt32(v)) nv = clampLL(nv, INT32_MIN, INT32_MAX);
        json::setInt(v, nv);
    } else {
        json::setFloat(v, json::asDouble(v) + static_cast<double>(delta));
    }
    markEdited();
}

void App::treeEdit(json::Value& v, const std::string& label) {
    switch (v.type) {
        case json::Type::Bool:
            json::setBool(v, !v.boolean);
            markEdited();
            return;
        case json::Type::Null: toast(tr("Wartości null nie można edytować", "A null value cannot be edited"), col::warn); return;
        case json::Type::Number: {
            if (!json::isFiniteNumber(v)) {
                toast(tr("Ta liczba (NaN/Infinity) nie jest edytowalna", "This number (NaN/Infinity) cannot be edited"), col::warn);
                return;
            }
            const bool isInt = json::isIntegerNumber(v);
            plat::TextRequest req;
            req.header = label;
            req.guide = isInt ? tr("Wpisz liczbę całkowitą", "Enter a whole number") : tr("Wpisz liczbę (np. 1,5)", "Enter a number (e.g. 1.5)");
            req.initial = v.text;
            req.numeric = true;
            req.allowNegative = true;
            req.allowDecimal = !isInt;
            req.maxLength = 24;
            std::string out;
            if (!plat::textInput(req, out)) return;
            if (isInt) {
                long long n = 0;
                if (!parseIntText(out, n)) {
                    toast(tr("To nie jest poprawna liczba całkowita", "That is not a valid whole number"), col::err);
                    return;
                }
                if (keepInt32(v) && (n < INT32_MIN || n > INT32_MAX)) {
                    n = clampLL(n, INT32_MIN, INT32_MAX);
                    toast(tr("Wartość ograniczono do zakresu liczby 32-bit", "Value limited to the 32-bit range"), col::warn);
                }
                json::setInt(v, n);
            } else {
                double d = 0;
                if (!parseFloatText(out, d)) {
                    toast(tr("To nie jest poprawna liczba", "That is not a valid number"), col::err);
                    return;
                }
                json::setFloat(v, d);
            }
            markEdited();
            return;
        }
        case json::Type::String: {
            plat::TextRequest req;
            req.header = label;
            req.guide = tr("Wpisz tekst", "Enter text");
            req.initial = v.text;
            req.maxLength = static_cast<int>(std::max<size_t>(256, v.text.size() + 64));
            std::string out;
            if (!plat::textInput(req, out)) return;
            json::setString(v, out);
            markEdited();
            return;
        }
        default: return;
    }
}

void App::treeContextMenu() {
    json::Value* cont = resolveTree();
    rebuildTreeRows(cont);
    if (treeRows_.empty()) return;
    const size_t idx = treeRows_[static_cast<size_t>(tree_.back().list.sel)];
    json::Value* v = treeChild(cont, idx);
    if (!v) return;
    Modal m;
    m.title = childLabel(*cont, idx);
    if (v->isScalar() && v->modified) {
        MenuItem it;
        it.label = tr("Przywróć oryginalną wartość", "Restore the original value");
        it.sub = tr("Oryginał: ", "Original: ") + json::originalDisplay(*v);
        it.action = [this, idx] {
            json::Value* t = treeChild(resolveTree(), idx);
            if (t) {
                json::revert(*t);
                markEdited();
                toast(tr("Przywrócono oryginalną wartość", "Original value restored"));
            }
        };
        m.items.push_back(it);
    }
    if (v->isContainer()) {
        const size_t changed = json::countModified(*v);
        if (changed > 0) {
            MenuItem it;
            it.label = tr("Cofnij zmiany w tym elemencie", "Undo the changes in this element");
            it.sub = i18n::plural(static_cast<long long>(changed), "zmieniona wartość", "zmienione wartości",
                                  "zmienionych wartości", "changed value", "changed values");
            it.action = [this, idx] {
                json::Value* t = treeChild(resolveTree(), idx);
                if (t) {
                    revertTree(*t);
                    markEdited();
                    toast(tr("Cofnięto zmiany w elemencie", "Changes in the element undone"));
                }
            };
            m.items.push_back(it);
        }
    }
    if (cont->type == json::Type::Array) {
        MenuItem dup;
        dup.label = tr("Duplikuj element", "Duplicate the element");
        dup.sub = tr("Wstawia kopię tego elementu zaraz pod nim", "Inserts a copy right below it");
        dup.action = [this, idx] {
            json::Value* c = resolveTree();
            if (!c || c->type != json::Type::Array || idx >= c->items.size()) return;
            json::Value copy = c->items[idx];
            json::clearModified(copy);
            copy.added = true;
            c->items.insert(c->items.begin() + static_cast<std::ptrdiff_t>(idx) + 1, copy);
            markStructureChanged();
            if (tree_.back().filter.empty()) tree_.back().list.sel = static_cast<int>(idx) + 1;
            toast(tr("Zduplikowano element [", "Duplicated element [") + std::to_string(idx) + "]");
        };
        m.items.push_back(dup);
        MenuItem del;
        del.label = tr("Usuń element", "Delete the element");
        del.sub = tr("Usuwa ten element z listy", "Removes this element from the list");
        del.danger = true;
        del.action = [this, idx] {
            showConfirm(tr("Usunąć element?", "Delete the element?"),
                        tr("Element [", "Element [") + std::to_string(idx) +
                            tr("] zostanie usunięty z listy.", "] will be removed from the list."),
                        tr("Usuń", "Delete"),
                        [this, idx] {
                            json::Value* c = resolveTree();
                            if (!c || c->type != json::Type::Array || idx >= c->items.size()) return;
                            if (!c->items[idx].added) doc_->removed.push_back(pathLabel(treeSegs(), idx));
                            c->items.erase(c->items.begin() + static_cast<std::ptrdiff_t>(idx));
                            markStructureChanged();
                            toast(tr("Usunięto element [", "Deleted element [") + std::to_string(idx) + "]");
                        },
                        true);
        };
        m.items.push_back(del);
    }
    if (m.items.empty()) {
        toast(tr("Brak dodatkowych akcji dla tego elementu", "No extra actions for this element"));
        return;
    }
    MenuItem cancel;
    cancel.label = tr("Anuluj", "Cancel");
    m.items.push_back(cancel);
    modals_.push_back(m);
}

void App::updateTree(const plat::Input& in, const Nav& nav) {
    json::Value* cont = resolveTree();
    rebuildTreeRows(cont);
    TreeLevel& lvl = tree_.back();
    const int visible = (kEditH - kTreeListTop - 8) / kTreeRowH;
    moveList(lvl.list, static_cast<int>(treeRows_.size()), nav, in, visible);

    if (in.down & plat::BTN_Y) {
        plat::TextRequest req;
        req.header = tr("Szukaj w tym widoku", "Search in this view");
        req.guide = tr("Fragment nazwy lub wartości (puste = pokaż wszystko)", "Part of a name or value (empty = show all)");
        req.initial = lvl.filter;
        req.maxLength = 64;
        std::string out;
        if (plat::textInput(req, out)) {
            tree_.back().filter = out;
            tree_.back().list = ListState();
        }
        return;
    }
    if (in.down & plat::BTN_B) {
        if (!lvl.filter.empty()) {
            lvl.filter.clear();
            lvl.list = ListState();
        } else if (tree_.size() > 1) {
            tree_.pop_back();
        } else {
            requestLeaveEditor();
        }
        return;
    }
    if (treeRows_.empty()) return;
    const size_t idx = treeRows_[static_cast<size_t>(lvl.list.sel)];
    json::Value* v = treeChild(cont, idx);
    if (!v) return;
    if (nav.left || nav.right) treeAdjust(*v, nav.right ? nav.step : -nav.step);
    if (in.down & plat::BTN_A) {
        if (v->isContainer()) {
            TreeLevel next;
            next.seg.isIndex = cont->type == json::Type::Array;
            next.seg.index = idx;
            if (!next.seg.isIndex) next.seg.key = cont->members[idx].key;
            tree_.push_back(next);
        } else {
            treeEdit(*v, childLabel(*cont, idx));
        }
    } else if (in.down & plat::BTN_X) {
        treeContextMenu();
    }
}

// =====================================================================================
// Editor - actions tab
// =====================================================================================

int App::actionRowCount() { return static_cast<int>(silk::bulkActions().size()) + 2 + (doc_->undoRoot ? 1 : 0); }

const std::vector<int>& App::actionCounts() {
    if (actionCountsEdit_ != editCounter_ || actionCounts_.size() != silk::bulkActions().size()) {
        actionCounts_.clear();
        for (const silk::BulkAction& a : silk::bulkActions()) actionCounts_.push_back(a.run(doc_->root, false));
        actionCountsEdit_ = editCounter_;
    }
    return actionCounts_;
}

void App::snapshotUndo(const std::string& label) {
    doc_->undoRoot.reset(new json::Value(doc_->root));
    doc_->undoRemoved = doc_->removed;
    doc_->undoImportedFrom = doc_->importedFrom;
    doc_->undoLabel = label;
}

void App::runAction(size_t idx) {
    const std::vector<silk::BulkAction>& acts = silk::bulkActions();
    if (idx >= acts.size()) return;
    const int n = acts[idx].run(doc_->root, false);
    if (n == 0) {
        toast(tr("Nic do zmiany – wszystko jest już ustawione", "Nothing to change - everything is already set"), col::ok);
        return;
    }
    showConfirm(acts[idx].label,
                acts[idx].help + tr("\n\nZmieni: ", "\n\nChanges: ") +
                    i18n::plural(n, "wpis", "wpisy", "wpisów", "entry", "entries") +
                    tr(". Można to cofnąć w tej karcie, a zmiany trafią do zapisu dopiero po zapisaniu (+).",
                       ". It can be undone on this tab, and nothing is written before you save (+)."),
                tr("Wykonaj", "Run"), [this, idx] {
                    const silk::BulkAction& a = silk::bulkActions()[idx];
                    snapshotUndo(a.label);
                    const int done = a.run(doc_->root, true);
                    markStructureChanged();  // refresh cached row pointers
                    toast(a.label + ": " + i18n::plural(done, "zmiana", "zmiany", "zmian", "change", "changes"), col::ok);
                    logf("action '%s': %d", a.label.c_str(), done);
                });
}

void App::undoAction() {
    if (!doc_ || !doc_->undoRoot) return;
    doc_->root = std::move(*doc_->undoRoot);
    doc_->undoRoot.reset();
    doc_->removed = doc_->undoRemoved;
    doc_->importedFrom = doc_->undoImportedFrom;
    markStructureChanged();
    toast(tr("Cofnięto: ", "Undone: ") + doc_->undoLabel);
}

void App::importMenu() {
    const std::string dir = plat::sdPath(kImportDirSd);
    fsu::mkdirs(dir);
    std::vector<fsu::Entry> entries;
    fsu::listDir(dir, entries);
    std::sort(entries.begin(), entries.end(), [](const fsu::Entry& a, const fsu::Entry& b) { return a.name < b.name; });
    Modal m;
    m.title = tr("Importuj zapis z PC", "Import a PC save");
    m.text = tr("Plik zastąpi zawartość ", "The file replaces the content of ") + doc_->entry.name +
             tr(" (zapisze się w formacie tego pliku).", " (it is stored in this file's format).");
    for (const fsu::Entry& e : entries) {
        if (e.isDir || e.name.empty() || e.name[0] == '.' || e.size == 0 || e.size > 64u * 1024u * 1024u) continue;
        MenuItem it;
        it.label = e.name;
        it.sub = formatSize(e.size);
        const std::string path = fsu::join(dir, e.name);
        it.action = [this, path] { importFile(path); };
        m.items.push_back(it);
    }
    if (m.items.empty()) {
        showMessage(tr("Brak plików do importu", "No files to import"),
                    tr("Skopiuj plik zapisu z PC (np. user1.dat) do folderu:\n", "Copy a PC save file (e.g. user1.dat) to the folder:\n") +
                        std::string(kImportDirSd) +
                        tr("\n\nZapisy PC znajdziesz w %USERPROFILE%\\AppData\\LocalLow\\Team Cherry\\Hollow Knight "
                           "Silksong\\ (w podfolderze z numerem konta). Potem wybierz tę akcję ponownie.",
                           "\n\nPC saves are in %USERPROFILE%\\AppData\\LocalLow\\Team Cherry\\Hollow Knight Silksong\\ "
                           "(in the sub-folder with your account number). Then run this action again."));
        return;
    }
    MenuItem cancel;
    cancel.label = tr("Anuluj", "Cancel");
    m.items.push_back(cancel);
    modals_.push_back(m);
}

void App::importFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!fsu::readFile(path, bytes, err)) {
        showMessage(tr("Błąd odczytu", "Read error"), err, col::err);
        return;
    }
    SaveCodecInfo info;
    auto root = std::make_shared<json::Value>();
    if (!loadSave(bytes, info, *root, err)) {
        showMessage(tr("Nie można zaimportować", "Cannot import"), fsu::fileName(path) + ":\n" + err, col::err);
        return;
    }
    if (!root->find("playerData")) {
        showMessage(tr("Nie można zaimportować", "Cannot import"),
                    tr("Plik nie wygląda na zapis Silksong (brak sekcji playerData).",
                       "The file does not look like a Silksong save (no playerData section)."),
                    col::err);
        return;
    }
    const std::string name = fsu::fileName(path);
    showConfirm(tr("Zastąpić zawartość pliku?", "Replace the file content?"),
                doc_->entry.name + tr(" otrzyma zawartość pliku ", " gets the content of ") + name + " (" +
                    saveFormatName(info.format) +
                    tr("). Do zapisu trafi to dopiero po zapisaniu (+). Można cofnąć w karcie Akcje.",
                       "). Nothing is written before you save (+). It can be undone on the Actions tab."),
                tr("Importuj", "Import"), [this, root, name] {
                    snapshotUndo(tr("Import zapisu z PC", "PC save import"));
                    doc_->root = std::move(*root);
                    doc_->importedFrom = name;
                    doc_->removed.clear();
                    markStructureChanged();
                    toast(tr("Zaimportowano ", "Imported ") + name, col::ok);
                    logf("imported %s into %s", name.c_str(), doc_->entry.name.c_str());
                });
}

void App::exportSave() {
    const bool toPc = doc_->codec.format == SaveFormat::PlainJson;
    SaveCodecInfo target;
    target.format = toPc ? SaveFormat::PcEncrypted : SaveFormat::PlainJson;
    std::vector<uint8_t> bytes;
    std::string err;
    if (!buildSave(target, doc_->root, bytes, err)) {
        showMessage(tr("Błąd eksportu", "Export error"), err, col::err);
        return;
    }
    const std::string dir = fsu::join(plat::sdPath(kExportDirSd), jksv::safeFileName(plat::localTimeStamp()));
    const std::string file = fsu::join(dir, fsu::fileName(doc_->entry.name));
    if (!fsu::mkdirs(dir) || !fsu::writeVerified(file, bytes.data(), bytes.size(), err)) {
        showMessage(tr("Błąd eksportu", "Export error"), err.empty() ? dir : err, col::err);
        return;
    }
    // independent check: the written file decodes to the same document
    SaveCodecInfo back;
    json::Value check;
    std::string a, b;
    if (!loadSave(bytes, back, check, err) || back.format != target.format) {
        showMessage(tr("Błąd eksportu", "Export error"), err, col::err);
        return;
    }
    json::serialize(check, a);
    json::serialize(doc_->root, b);
    if (a != b) {
        showMessage(tr("Błąd eksportu", "Export error"), tr("Niezgodność danych po eksporcie.", "Data mismatch after export."), col::err);
        return;
    }
    logf("exported %s as %s", file.c_str(), saveFormatName(target.format));
    if (toPc) {
        showMessage(tr("Wyeksportowano zapis PC", "PC save exported"),
                    tr("Plik:\n", "File:\n") + displaySd(file) +
                        tr("\n\nSkopiuj go na komputer do folderu zapisów gry: %USERPROFILE%\\AppData\\LocalLow\\Team "
                           "Cherry\\Hollow Knight Silksong\\<numer konta>\\ i zastąp plik o tej samej nazwie (zrób "
                           "wcześniej kopię). Zgodność zapisów Switch i PC nie jest gwarantowana.",
                           "\n\nCopy it to the game's save folder on the PC: %USERPROFILE%\\AppData\\LocalLow\\Team "
                           "Cherry\\Hollow Knight Silksong\\<account number>\\ and replace the file with the same name "
                           "(back it up first). Switch and PC saves are not guaranteed to be compatible."),
                    col::ok);
    } else {
        showMessage(tr("Wyeksportowano zapis Switch", "Switch save exported"),
                    tr("Plik (zwykły JSON, format Switcha):\n", "File (plain JSON, Switch format):\n") + displaySd(file),
                    col::ok);
    }
}

void App::updateActions(const plat::Input& in, const Nav& nav) {
    const int nBulk = static_cast<int>(silk::bulkActions().size());
    const int count = actionRowCount();
    const int visible = (kEditH - 16) / kActionRowH;
    moveList(actionList_, count, nav, in, visible);
    if (in.down & plat::BTN_A) {
        const int sel = actionList_.sel;
        if (sel < nBulk) runAction(static_cast<size_t>(sel));
        else if (sel == nBulk) importMenu();
        else if (sel == nBulk + 1) exportSave();
        else undoAction();
    } else if (in.down & plat::BTN_B) {
        requestLeaveEditor();
    }
}

// =====================================================================================
// Editor - changes tab
// =====================================================================================

void App::revertChange(size_t idx) {
    const std::vector<Change>& list = changes();
    if (idx >= list.size()) return;
    const Change c = list[idx];
    switch (c.kind) {
        case Change::Value:
            json::revert(*c.node);
            markEdited();
            toast(tr("Przywrócono oryginał: ", "Original restored: ") + c.before);
            break;
        case Change::Bytes:
            for (size_t i = c.index; i < c.index + c.length && i < doc_->bytes.size() && i < doc_->orig.size(); ++i)
                doc_->bytes[i] = doc_->orig[i];
            markEdited();
            toast(tr("Przywrócono oryginał: ", "Original restored: ") + c.before);
            break;
        case Change::Added:
            if (c.parent->type == json::Type::Array && c.index < c.parent->items.size())
                c.parent->items.erase(c.parent->items.begin() + static_cast<std::ptrdiff_t>(c.index));
            else if (c.parent->type == json::Type::Object && c.index < c.parent->members.size())
                c.parent->members.erase(c.parent->members.begin() + static_cast<std::ptrdiff_t>(c.index));
            markStructureChanged();
            toast(tr("Usunięto dodany wpis", "Added entry removed"));
            break;
        default:
            toast(tr("Tę zmianę cofniesz tylko przez „Odrzuć wszystkie zmiany” (+)", "Undo this one with \"Discard all changes\" (+)"),
                  col::warn);
            break;
    }
}

void App::showChangeInTree(size_t idx) {
    const std::vector<Change>& list = changes();
    if (idx < list.size() && list[idx].kind == Change::Bytes) {
        hexCursor_ = list[idx].index;
        hexScroll_ = static_cast<int>(hexCursor_ / 16) - 3;
        if (hexScroll_ < 0) hexScroll_ = 0;
        tab_ = TabHex;
        return;
    }
    if (idx >= list.size() || (list[idx].kind != Change::Value && list[idx].kind != Change::Added)) return;
    const Change& c = list[idx];
    tree_.clear();
    tree_.push_back(TreeLevel());
    for (const PathSeg& s : c.segs) {
        TreeLevel l;
        l.seg = s;
        tree_.push_back(l);
    }
    tree_.back().list.sel = static_cast<int>(c.index);
    tree_.back().list.scroll = std::max(0, static_cast<int>(c.index) - 3);
    treeRowsFor_ = nullptr;
    tab_ = TabTree;
}

void App::updateChanges(const plat::Input& in, const Nav& nav) {
    const int n = static_cast<int>(changes().size());
    const int visible = (kEditH - 16) / kChangeRowH;
    moveList(changeList_, n, nav, in, visible);
    if (in.down & plat::BTN_B) {
        requestLeaveEditor();
        return;
    }
    if (n == 0) return;
    if (in.down & plat::BTN_A) showChangeInTree(static_cast<size_t>(changeList_.sel));
    else if (in.down & plat::BTN_X) revertChange(static_cast<size_t>(changeList_.sel));
}

// =====================================================================================
// Saving
// =====================================================================================

void App::openSaveMenu() {
    const int count = changeCount();
    Modal m;
    m.title = tr("Zapisz zmiany", "Save changes");
    m.text = (doc_->backup.isLive ? tr("Zapis w grze: ", "Game save: ") + doc_->backup.userName
                                  : tr("Kopia: ", "Backup: ") + doc_->backup.name) +
             tr("\nPlik: ", "\nFile: ") + doc_->entry.name + tr("\nZmiany: ", "\nChanges: ") + std::to_string(count);
    // which game a document can be written to: the open game, or the game a safety copy came from
    uint64_t targetTitle = title_.titleId;
    std::vector<plat::SaveUser> targets = liveUsers_;
    if (!targetTitle && title_.isSafety && !doc_->backup.isLive) {
        std::vector<uint8_t> src;
        std::string e2;
        json::Value info;
        size_t end = 0;
        json::ParseError pe;
        if (fsu::readFile(fsu::join(doc_->backup.path, ".sse_source"), src, e2) &&
            json::parse(reinterpret_cast<const char*>(src.data()), src.size(), info, end, pe)) {
            const json::Value* t = info.find("title");
            if (t && t->type == json::Type::String && mods::parseTitleHex(t->text, targetTitle)) {
                std::string e3;
                if (!plat::liveUsers(targetTitle, targets, e3)) targets.clear();
            }
        }
    }
    auto addGameTarget = [this, &m, targetTitle](const plat::SaveUser& u, bool primary) {
        MenuItem it;
        const jksv::Backup live = jksv::liveBackup(targetTitle, u);
        it.label = primary ? tr("Zapisz w grze (zalecane)", "Save to the game (recommended)")
                           : tr("Wgraj do gry – ", "Write to the game – ") + u.name;
        it.sub = tr("Poprzedni stan zapisu gry trafi do ", "The previous game save goes to ") + std::string(kSafetyDirSd);
        it.action = [this, live] {
            showConfirm(tr("Zapisać w grze?", "Save to the game?"),
                        doc_->entry.name + tr(" zostanie zapisany prosto w zapisie gry użytkownika ",
                                              " is written directly into the game save of ") +
                            live.userName +
                            tr(". Gra musi być zamknięta. Najpierw cały obecny zapis gry zostanie skopiowany do ",
                               ". The game must be closed. First the whole current game save is copied to ") +
                            std::string(kSafetyDirSd) + ".",
                        tr("Zapisz w grze", "Save to the game"), [this, live] {
                            runBusy(tr("Zapisywanie w grze...", "Saving to the game..."), [this, live] { saveToGame(live); });
                        });
        };
        m.items.push_back(it);
    };
    if (doc_->backup.isLive) {
        for (const plat::SaveUser& u : liveUsers_)
            if (u.id == doc_->backup.userId) addGameTarget(u, true);
        MenuItem asNew;
        asNew.label = tr("Zapisz jako NOWĄ kopię JKSV", "Save as a NEW JKSV backup");
        const jksv::TitleDir* st = &title_;
        if (st->exists && !st->path.empty()) {
            asNew.sub = tr("W folderze JKSV: ", "In the JKSV folder: ") + st->name + tr(" (gra bez zmian)", " (game unchanged)");
            asNew.action = [this] { runBusy(tr("Zapisywanie nowej kopii...", "Saving a new backup..."), [this] { saveLiveAsNew(); }); };
        } else {
            asNew.enabled = false;
            asNew.sub = tr("Ta gra nie ma folderu w JKSV – zrób w JKSV jedną kopię", "This game has no JKSV folder - make one backup in JKSV");
        }
        m.items.push_back(asNew);
        for (const plat::SaveUser& u : liveUsers_)
            if (u.id != doc_->backup.userId) addGameTarget(u, false);
    } else {
        MenuItem asNew;
        asNew.label = tr("Zapisz jako NOWĄ kopię JKSV (zalecane)", "Save as a NEW JKSV backup (recommended)");
        asNew.sub = tr("Utworzy: ", "Creates: ") + jksv::makeEditedName(doc_->backup, plat::localTimeStamp());
        asNew.action = [this] { runBusy(tr("Zapisywanie nowej kopii...", "Saving a new backup..."), [this] { saveAsNew(); }); };
        m.items.push_back(asNew);
        MenuItem over;
        over.label = tr("Nadpisz tę kopię", "Overwrite this backup");
        over.sub = tr("Oryginał zostanie zachowany w ", "The original is kept in ") + std::string(kSafetyDirSd);
        over.action = [this] {
            showConfirm(tr("Nadpisać kopię?", "Overwrite the backup?"),
                        tr("Kopia „", "The backup \"") + doc_->backup.name +
                            tr("” zostanie zastąpiona edytowaną wersją. Oryginał zostanie wcześniej skopiowany do ",
                               "\" is replaced by the edited version. The original is copied first to ") +
                            std::string(kSafetyDirSd) + ".",
                        tr("Nadpisz", "Overwrite"), [this] { runBusy(tr("Zapisywanie...", "Saving..."), [this] { saveOverwrite(); }); },
                        true);
        };
        m.items.push_back(over);
        for (const plat::SaveUser& u : targets) addGameTarget(u, false);
    }
    MenuItem list;
    list.label = tr("Pokaż listę zmian", "Show the list of changes");
    list.sub = i18n::plural(count, "zmiana", "zmiany", "zmian", "change", "changes");
    list.enabled = count > 0;
    list.action = [this] { tab_ = TabChanges; };
    m.items.push_back(list);
    MenuItem discard;
    discard.label = tr("Odrzuć wszystkie zmiany", "Discard all changes");
    discard.sub = doc_->backup.isLive ? tr("Wczytaj ponownie zapis gry", "Reload the game save")
                                      : tr("Wczytaj ten zapis ponownie z karty SD", "Reload this save from the SD card");
    discard.enabled = docDirty();
    discard.danger = true;
    discard.action = [this] {
        showConfirm(tr("Odrzucić zmiany?", "Discard the changes?"),
                    tr("Wszystkie niezapisane zmiany zostaną utracone.", "All unsaved changes will be lost."),
                    tr("Odrzuć zmiany", "Discard the changes"),
                    [this] {
                        const jksv::Backup b = doc_->backup;
                        const jksv::SaveEntry e = doc_->entry;
                        runBusy(tr("Wczytywanie...", "Loading..."), [this, b, e] {
                            loadDoc(b, e);
                            toast(tr("Wczytano ponownie – zmiany odrzucone", "Reloaded - changes discarded"));
                        });
                    },
                    true);
    };
    m.items.push_back(discard);
    MenuItem cancel;
    cancel.label = tr("Anuluj", "Cancel");
    m.items.push_back(cancel);
    modals_.push_back(m);
}

bool App::docBytes(std::vector<uint8_t>& out, std::string& err, bool forGame) {
    if (doc_->binary) {
        out = doc_->bytes;
        return true;
    }
    // the console version of the Team Cherry games stores plain JSON
    SaveCodecInfo codec = doc_->codec;
    if (forGame && codec.format != SaveFormat::PlainJson) codec = SaveCodecInfo();
    return buildSave(codec, doc_->root, out, err);
}

void App::markSaved() {
    if (doc_->binary) doc_->orig = doc_->bytes;
    json::clearModified(doc_->root);
    doc_->removed.clear();
    doc_->importedFrom.clear();
    doc_->undoRoot.reset();
    ++editCounter_;
}

void App::saveAsNew() {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!docBytes(bytes, err, false)) {
        showMessage(tr("Błąd zapisu", "Save error"), err, col::err);
        logf("build failed: %s", err.c_str());
        return;
    }
    const std::string name = jksv::makeEditedName(doc_->backup, plat::localTimeStamp());
    jksv::Backup created;
    if (!jksv::saveAsNewBackup(doc_->backup, doc_->entry.name, bytes, name, created, err)) {
        showMessage(tr("Błąd zapisu", "Save error"), err, col::err);
        logf("save as new failed: %s", err.c_str());
        return;
    }
    std::vector<uint8_t> back;
    if (!jksv::readSaveFile(created, doc_->entry.name, back, err) || back != bytes) {
        showMessage(tr("Błąd weryfikacji", "Verification error"),
                    tr("Nowa kopia nie przeszła weryfikacji. ", "The new backup failed verification. ") + err, col::err);
        logf("verify failed: %s", err.c_str());
        return;
    }
    doc_->backup = created;
    backup_ = created;
    markSaved();
    reloadBackups();
    for (size_t i = 0; i < backups_.size(); ++i)
        if (!backups_[i].isLive && backups_[i].name == created.name) backupList_.sel = static_cast<int>(i);
    const std::string slot = doc_->entry.slot > 0 ? std::to_string(doc_->entry.slot) : doc_->entry.name;
    std::string how = tr("\n\nAby wgrać ją do gry:\n1. Wyjdź z edytora i uruchom JKSV.\n"
                         "2. Wybierz swojego użytkownika i Hollow Knight Silksong.\n3. Wybierz kopię „",
                         "\n\nTo put it into the game:\n1. Leave the editor and start JKSV.\n"
                         "2. Pick your user and Hollow Knight Silksong.\n3. Pick the backup \"") +
                      created.name +
                      tr("” i użyj Przywróć (Restore).\n4. Uruchom grę i wczytaj slot ", "\" and use Restore.\n4. Start the game and load slot ") +
                      slot + ".";
    if (!liveUsers_.empty())
        how += tr("\n\nAlbo bez JKSV: naciśnij + i wybierz „Wgraj do gry”.", "\n\nOr without JKSV: press + and pick \"Write to the game\".");
    showMessage(tr("Zapisano nową kopię", "New backup saved"), tr("Utworzono kopię:\n", "Created the backup:\n") + created.name + how, col::ok);
}

void App::saveOverwrite() {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!docBytes(bytes, err, false)) {
        showMessage(tr("Błąd zapisu", "Save error"), err, col::err);
        logf("build failed: %s", err.c_str());
        return;
    }
    std::string safety;
    const std::string safetyDir = plat::sdPath(kSafetyDirSd);
    if (!jksv::overwriteBackup(doc_->backup, doc_->entry.name, bytes, safetyDir, safety, err)) {
        showMessage(tr("Błąd zapisu", "Save error"), err, col::err);
        logf("overwrite failed: %s", err.c_str());
        return;
    }
    std::vector<uint8_t> back;
    if (!jksv::readSaveFile(doc_->backup, doc_->entry.name, back, err) || back != bytes) {
        showMessage(tr("Błąd weryfikacji", "Verification error"),
                    tr("Zapis nie przeszedł weryfikacji. Oryginał znajdziesz w:\n", "The save failed verification. The original is in:\n") +
                        displaySd(safety) + "\n" + err,
                    col::err);
        logf("verify failed: %s", err.c_str());
        return;
    }
    if (!doc_->backup.path.empty() && fsu::parentDir(doc_->backup.path) != safetyDir)
        jksv::pruneBackups(safetyDir, settings::get().keepBackups);
    markSaved();
    reloadBackups();
    showMessage(tr("Zapisano", "Saved"),
                tr("Kopia „", "The backup \"") + doc_->backup.name + tr("” została nadpisana.\nOryginał zachowano w:\n", "\" was overwritten.\nThe original is kept in:\n") +
                    displaySd(safety) +
                    tr("\n\nW JKSV wybierz tę kopię i użyj Przywróć (Restore), aby wgrać ją do gry.",
                       "\n\nIn JKSV pick this backup and use Restore to put it into the game."),
                col::ok);
}

void App::saveToGame(const jksv::Backup& live) {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!docBytes(bytes, err, true)) {
        showMessage(tr("Błąd zapisu", "Save error"), err, col::err);
        return;
    }
    std::string safety;
    const std::string safetyDir = plat::sdPath(kSafetyDirSd);
    if (!jksv::writeLive(live, doc_->entry.name, bytes, safetyDir, safety, err)) {
        logf("save to game failed: %s", err.c_str());
        showMessage(tr("Nie zapisano w grze", "Not saved to the game"),
                    err + (safety.empty() ? std::string() : tr("\n\nKopia bezpieczeństwa: ", "\n\nSafety copy: ") + displaySd(safety)),
                    col::err);
        return;
    }
    // never prune the safety copy that is open in the editor right now (restore from a safety copy)
    if (doc_->backup.isLive || fsu::parentDir(doc_->backup.path) != safetyDir)
        jksv::pruneBackups(safetyDir, settings::get().keepBackups);
    const bool sameSave = doc_->backup.isLive && doc_->backup.userId == live.userId;
    if (sameSave) markSaved();
    const std::string slot = doc_->entry.slot > 0 ? std::to_string(doc_->entry.slot) : doc_->entry.name;
    showMessage(tr("Zapisano w grze", "Saved to the game"),
                tr("Zapis gry użytkownika ", "The game save of ") + live.userName + tr(" został zmieniony.\nUruchom Silksong i wczytaj slot ", " was changed.\nStart Silksong and load slot ") +
                    slot + tr(".\n\nPoprzedni stan zapisu gry:\n", ".\n\nThe previous game save:\n") + displaySd(safety) +
                    (sameSave ? std::string() : tr("\n\nKopia, z której pochodzą dane, nie została zmieniona.", "\n\nThe backup the data came from was not changed.")),
                col::ok);
}

void App::saveLiveAsNew() {
    const jksv::TitleDir* st = &title_;
    if (!st->exists || st->path.empty()) return;
    std::vector<uint8_t> bytes;
    std::string err;
    if (!docBytes(bytes, err, false)) {
        showMessage(tr("Błąd zapisu", "Save error"), err, col::err);
        return;
    }
    const std::string name = jksv::makeLiveCopyName(st->path, doc_->backup, plat::localTimeStamp());
    jksv::Backup created;
    if (!jksv::saveLiveAsNewBackup(doc_->backup, doc_->entry.name, bytes, st->path, name, created, err)) {
        showMessage(tr("Błąd zapisu", "Save error"), err, col::err);
        return;
    }
    std::vector<uint8_t> back;
    if (!jksv::readSaveFile(created, doc_->entry.name, back, err) || back != bytes) {
        showMessage(tr("Błąd weryfikacji", "Verification error"),
                    tr("Nowa kopia nie przeszła weryfikacji. ", "The new backup failed verification. ") + err, col::err);
        return;
    }
    showMessage(tr("Zapisano nową kopię", "New backup saved"),
                tr("Utworzono kopię JKSV:\n", "Created the JKSV backup:\n") + created.name +
                    tr("\n\nZapis w grze nie został zmieniony. Zmiany wciąż są w edytorze – możesz je też zapisać w grze.",
                       "\n\nThe game save was not changed. The changes are still in the editor - you can save them to the game too."),
                col::ok);
}

// =====================================================================================
// Browser menu (X)
// =====================================================================================

void App::openBrowserMenu() {
    Modal m;
    m.title = "Menu";
    if (screen_ == Screen::Backups && gameTab_ == 1 && title_.titleId) {
        MenuItem imp;
        imp.label = tr("Importuj mod z karty SD (.zip)", "Import a mod from the SD card (.zip)");
        imp.sub = std::string(kModImportDirSd);
        imp.action = [this] { importModMenu(); };
        m.items.push_back(imp);
    }
    if (screen_ == Screen::Titles && homeTab_ == 1) {
        MenuItem all;
        all.label = repoAllGames_ ? tr("Pokaż tylko mody do moich gier", "Show only mods for my games")
                                  : tr("Pokaż mody do wszystkich gier", "Show mods for all games");
        all.action = [this] {
            repoAllGames_ = !repoAllGames_;
            repoList_ = ListState();
            rebuildRepoRows();
        };
        m.items.push_back(all);
    }
    if ((screen_ == Screen::Titles && homeTab_ == 1) || (screen_ == Screen::Backups && gameTab_ == 1)) {
        MenuItem refresh;
        refresh.label = tr("Odśwież listę modów online", "Refresh the online mod list");
        refresh.sub = std::string("github.com/") + repo::kRepo;
        refresh.action = [this] {
            ensureRepo(true, [this] {
                rebuildRepoRows();
                rebuildModRows();
            });
        };
        m.items.push_back(refresh);
    }
    if (screen_ == Screen::Backups && title_.isSafety && !backups_.empty()) {
        const jksv::Backup b = backups_[static_cast<size_t>(backupList_.sel)];
        MenuItem del;
        del.label = tr("Usuń zaznaczoną kopię", "Delete the selected copy");
        del.sub = b.name;
        del.danger = true;
        del.action = [this, b] {
            showConfirm(tr("Usunąć kopię?", "Delete the copy?"),
                        b.name + tr("\n\nKopia zostanie trwale usunięta z karty SD.", "\n\nThe copy is permanently deleted from the SD card."),
                        tr("Usuń", "Delete"), [this, b] {
                            if (jksv::removeBackup(b)) toast(tr("Usunięto kopię", "Copy deleted"), col::ok);
                            else toast(tr("Nie udało się usunąć kopii", "Could not delete the copy"), col::err);
                            reloadBackups();
                        },
                        true);
        };
        m.items.push_back(del);
        const int keep = settings::get().keepBackups;
        MenuItem prune;
        prune.label = keep > 0 ? tr("Usuń stare kopie (zostaw ", "Delete old copies (keep ") + std::to_string(keep) + tr(" najnowszych)", " newest)")
                               : tr("Usuń stare kopie", "Delete old copies");
        prune.enabled = keep > 0 && static_cast<int>(backups_.size()) > keep;
        prune.sub = prune.enabled ? i18n::plural(static_cast<long long>(backups_.size()) - keep, "kopia do usunięcia", "kopie do usunięcia",
                                                 "kopii do usunięcia", "copy to delete", "copies to delete")
                                  : tr("Nic do usunięcia przy obecnym limicie", "Nothing to delete with the current limit");
        prune.danger = true;
        prune.action = [this, keep] {
            showConfirm(tr("Usunąć stare kopie?", "Delete old copies?"),
                        tr("Zostanie ", "The ") + std::to_string(keep) +
                            tr(" najnowszych kopii bezpieczeństwa, starsze zostaną trwale usunięte.", " newest safety copies stay, older ones are permanently deleted."),
                        tr("Usuń stare", "Delete old"), [this, keep] {
                            const int n = jksv::pruneBackups(title_.path, keep);
                            toast(i18n::plural(n, "usunięta kopia", "usunięte kopie", "usuniętych kopii", "copy deleted", "copies deleted"), col::ok);
                            reloadBackups();
                        },
                        true);
        };
        m.items.push_back(prune);
    }
    MenuItem lang;
    lang.label = tr("Język: polski", "Language: English");
    lang.sub = tr("Przełącz na angielski (English)", "Switch to Polish (polski)");
    lang.action = [this] {
        settings::get().language = i18n::english() ? i18n::Lang::Polish : i18n::Lang::English;
        i18n::setLanguage(settings::get().language);
        settings::save(plat::sdPath(kSettingsSd));
        actionCounts_.clear();
        toast(tr("Język: polski", "Language: English"), col::ok);
    };
    m.items.push_back(lang);
    const int keep = settings::get().keepBackups;
    MenuItem keepItem;
    keepItem.label = tr("Kopie bezpieczeństwa: ", "Safety copies: ") +
                     (keep > 0 ? tr("zostaw ", "keep ") + std::to_string(keep) + tr(" najnowszych", " newest") : std::string(tr("zostaw wszystkie", "keep all")));
    keepItem.sub = tr("Starsze są usuwane automatycznie po każdym nowym zapisie (A = zmień)",
                      "Older ones are deleted automatically after each new save (A = change)");
    keepItem.action = [this] {
        settings::get().keepBackups = settings::nextKeepValue(settings::get().keepBackups);
        settings::save(plat::sdPath(kSettingsSd));
        const int k = settings::get().keepBackups;
        toast(k > 0 ? tr("Zostawiam ", "Keeping ") + std::to_string(k) + tr(" najnowszych kopii", " newest copies") : std::string(tr("Zostawiam wszystkie kopie", "Keeping all copies")),
              col::ok);
        openBrowserMenu();
    };
    m.items.push_back(keepItem);
    MenuItem autoUpd;
    autoUpd.label = tr("Automatyczne aktualizacje: ", "Automatic updates: ") +
                    std::string(settings::get().autoUpdate ? tr("włączone", "on") : tr("wyłączone", "off"));
    autoUpd.sub = tr("Przy starcie sprawdza nową wersję na GitHubie (A = zmień)", "Checks GitHub for a new version at start (A = change)");
    autoUpd.action = [this] {
        settings::get().autoUpdate = !settings::get().autoUpdate;
        settings::save(plat::sdPath(kSettingsSd));
        openBrowserMenu();
    };
    m.items.push_back(autoUpd);
    MenuItem checkUpd;
    checkUpd.label = tr("Sprawdź aktualizację teraz", "Check for an update now");
    checkUpd.sub = std::string(tr("Wersja ", "Version ")) + APP_VERSION_STR;
    checkUpd.action = [this] { runBusy(tr("Sprawdzanie aktualizacji...", "Checking for updates..."), [this] { checkForUpdate(true); }); };
    m.items.push_back(checkUpd);
    MenuItem about;
    about.label = tr("O programie", "About");
    about.sub = std::string("Switch Mod Hub ") + APP_VERSION_STR;
    about.action = [this] {
        showMessage("Switch Mod Hub " APP_VERSION_STR,
                    tr("Zapisy i mody gier na Switchu.\nRepozytorium: github.com/", "Saves and mods for Switch games.\nRepository: github.com/") +
                        std::string(repo::kRepo) + tr("\n\nKopie bezpieczeństwa: ", "\n\nSafety copies: ") +
                        std::string(kSafetyDirSd) + tr("\nImport z PC: ", "\nPC import: ") + kImportDirSd +
                        tr("\nEksport: ", "\nExport: ") + kExportDirSd + tr("\nDefinicje (DLC): ", "\nDefinitions (DLC): ") +
                        kDefinitionsSd + tr("\nMody: ", "\nMods: ") + kModsDirSd + tr("\nLog: ", "\nLog: ") + "sdmc:/switch/SwitchModHub/log.txt");
    };
    m.items.push_back(about);
    MenuItem close;
    close.label = tr("Zamknij", "Close");
    m.items.push_back(close);
    modals_.push_back(m);
}

// =====================================================================================
// UI helpers
// =====================================================================================

void App::showMessage(const std::string& title, const std::string& text, uint32_t accent) {
    Modal m;
    m.title = title;
    m.text = text;
    m.accent = accent;
    modals_.push_back(m);
}

void App::showConfirm(const std::string& title, const std::string& text, const std::string& yes,
                      std::function<void()> onYes, bool danger) {
    Modal m;
    m.title = title;
    m.text = text;
    m.accent = danger ? col::warn : 0;
    MenuItem y;
    y.label = yes;
    y.danger = danger;
    y.action = std::move(onYes);
    MenuItem n;
    n.label = tr("Anuluj", "Cancel");
    m.items.push_back(y);
    m.items.push_back(n);
    m.list.sel = danger ? 1 : 0;
    modals_.push_back(m);
}

void App::toast(const std::string& text, uint32_t color) {
    Toast t;
    t.text = text;
    t.until = now_ + 2600;
    t.color = color ? color : col::gold;
    toasts_.push_back(t);
    if (toasts_.size() > 3) toasts_.erase(toasts_.begin());
}

void App::runBusy(const std::string& message, std::function<void()> task) {
    busyMessage_ = message;
    busyTask_ = std::move(task);
    busyFrames_ = 0;
}

void App::addHit(int x, int y, int w, int h, int list, int index, uint32_t buttons) {
    hits_.push_back({x, y, w, h, list, index, buttons});
}

void App::addArea(int x, int y, int w, int h, int list, int rowH, int visible, int count) {
    ScrollArea a;
    a.x = x;
    a.y = y;
    a.w = w;
    a.h = h;
    a.list = list;
    a.rowH = std::max(1, rowH);
    a.visible = std::max(1, visible);
    a.count = count;
    areas_.push_back(a);
}

// =====================================================================================
// Drawing - primitives
// =====================================================================================

void App::renderLayers() {
    for (int layer = 0; layer < 2; ++layer) {
        std::vector<uint32_t>& pixels = layer == 0 ? layerBrowse_ : layerEdit_;
        pixels.assign(static_cast<size_t>(kW) * kH, 0);
        Canvas bg;
        bg.attach(pixels.data(), kW, kH, kW);
        bg.fillGradientV(0, 0, kW, kH, col::bgTop, col::bgBottom);
        // soft crimson glow behind the header emblem
        for (int i = 0; i < 7; ++i) bg.circle(90.f, 30.f, 520.f - i * 70.f, hexColor(0x9E1F2E, 9));
        // silk threads
        struct Thread {
            float y, amp, freq, phase, width;
            uint32_t color;
        };
        const Thread threads[] = {
            {560.f, 60.f, 0.0045f, 0.5f, 1.4f, hexColor(0xE8C893, 26)},
            {600.f, 45.f, 0.0060f, 2.1f, 1.1f, hexColor(0xD8434F, 34)},
            {640.f, 30.f, 0.0038f, 4.0f, 1.0f, hexColor(0xE8C893, 16)},
        };
        for (const Thread& t : threads) {
            float px = -10.f, py = t.y + t.amp * std::sin(t.phase + px * t.freq);
            for (float x = 6.f; x <= kW + 16.f; x += 16.f) {
                const float y = t.y + t.amp * std::sin(t.phase + x * t.freq);
                bg.line(px, py, x, y, t.width, t.color);
                px = x;
                py = y;
            }
        }
        // header and footer bars
        bg.fillRect(0, 0, kW, kHeaderH, col::bar);
        bg.fillGradientH(0, kHeaderH - 2, kW, 2, withAlpha(col::accent, 220), withAlpha(col::accent, 0));
        drawEmblem(bg, 50.f, 40.f, 24.f);
        bg.fillRect(0, kFooterY, kW, kH - kFooterY, col::bar);
        bg.fillRect(0, kFooterY, kW, 1, col::panelBorder);
        // the two content panels
        const int py = layer == 0 ? kBrowseY : kEditY;
        const int ph = layer == 0 ? kBrowseH : kEditH;
        drawPanel(bg, kListX, py, kListW, ph);
        drawPanel(bg, kInfoX, py, kInfoW, ph);
    }
}

void App::drawBackground(Canvas& c) {
    const std::vector<uint32_t>& layer = (screen_ == Screen::Editor && doc_) ? layerEdit_ : layerBrowse_;
    uint32_t* dst = c.pixels();
    for (int y = 0; y < kH && y < c.height(); ++y)
        std::memcpy(dst + static_cast<size_t>(y) * c.stride(), layer.data() + static_cast<size_t>(y) * kW,
                    sizeof(uint32_t) * static_cast<size_t>(std::min(kW, c.width())));
}

void App::drawEmblem(Canvas& c, float cx, float cy, float r) {
    c.circle(cx, cy, r, col::accentDim);
    c.ring(cx, cy, r, 2.0f, col::accent);
    // needle from bottom-left to top-right
    const float dx = 0.7071f, dy = -0.7071f;
    const float tipX = cx + dx * r * 0.70f, tipY = cy + dy * r * 0.70f;
    const float baseX = cx - dx * r * 0.52f, baseY = cy - dy * r * 0.52f;
    const float w = r * 0.13f;
    const float px = 0.7071f * w, py = 0.7071f * w;
    const float needle[6] = {tipX, tipY, baseX + px, baseY + py, baseX - px, baseY - py};
    c.polygon(needle, 3, col::text);
    // silk thread: a gold arc inside the badge, starting at the needle's eye
    const float eyeX = baseX - dx * r * 0.10f, eyeY = baseY - dy * r * 0.10f;
    c.ring(eyeX, eyeY, r * 0.15f, 1.6f, col::text);
    float lx = eyeX, ly = eyeY;
    for (int i = 1; i <= 10; ++i) {
        const float a = 2.36f + i * 0.30f;  // sweep along the lower right part of the badge
        const float x = cx + std::cos(a) * r * 0.62f, y = cy + std::sin(a) * r * 0.62f;
        c.line(lx, ly, x, y, 1.5f, col::gold);
        lx = x;
        ly = y;
    }
}

void App::drawHeader(Canvas& c, const std::string& title, const std::string& subtitle) {
    // bar and emblem are part of the cached layer
    c.text(font_, 88, 9, 29, col::text, title, 760);
    c.text(font_, 89, 48, 17, col::dim, subtitle, 760);
}

void App::drawSteps(Canvas& c, int active) {
    const char* const labels[] = {tr("Gra", "Game"), tr("Kopia", "Save"), tr("Plik", "File"), tr("Edycja", "Edit")};
    int x = 878;
    for (int i = 0; i < 4; ++i) {
        const float cx = static_cast<float>(x) + 13.f, cy = 40.f;
        uint32_t fill = col::toggleOff, numColor = col::muted, labelColor = col::muted;
        if (i < active) {
            fill = col::accentDim;
            numColor = col::text;
            labelColor = col::dim;
        } else if (i == active) {
            fill = col::accent;
            numColor = col::text;
            labelColor = col::text;
        }
        const int labelEnd = x + 30 + font_.measure(labels[i], 16);
        if (i < 3 && x + 94 - 6 > labelEnd + 6)
            c.fillRect(labelEnd + 6, 39, x + 94 - 6 - (labelEnd + 6), 2, i < active ? col::accentDim : col::toggleOff);
        c.circle(cx, cy, 13.f, fill);
        c.text(font_, x, 29, 16, numColor, std::to_string(i + 1), 26, Align::Center);
        c.text(font_, x + 30, 30, 16, labelColor, labels[i]);
        x += 94;
    }
}

int App::buttonBadgeWidth(const std::string& btn) {
    if (btn == "A" || btn == "B" || btn == "X" || btn == "Y") return 28;
    if (btn == "UD" || btn == "LR") return 40;
    return std::max(30, font_.measure(btn, 15) + 16);
}

void App::drawArrow(Canvas& c, float cx, float cy, float s, int dir, uint32_t color) {
    // dir: 0 up, 1 down, 2 left, 3 right
    float p[6];
    switch (dir) {
        case 0: { const float q[6] = {cx, cy - s, cx + s, cy + s * 0.6f, cx - s, cy + s * 0.6f}; std::memcpy(p, q, sizeof(p)); break; }
        case 1: { const float q[6] = {cx, cy + s, cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f}; std::memcpy(p, q, sizeof(p)); break; }
        case 2: { const float q[6] = {cx - s, cy, cx + s * 0.6f, cy - s, cx + s * 0.6f, cy + s}; std::memcpy(p, q, sizeof(p)); break; }
        default: { const float q[6] = {cx + s, cy, cx - s * 0.6f, cy + s, cx - s * 0.6f, cy - s}; std::memcpy(p, q, sizeof(p)); break; }
    }
    c.polygon(p, 3, color);
}

int App::drawButtonBadge(Canvas& c, int x, int y, const std::string& btn) {
    const int w = buttonBadgeWidth(btn);
    const int h = 28;
    if (btn == "A" || btn == "B" || btn == "X" || btn == "Y") {
        c.circle(x + 14.f, y + 14.f, 14.f, col::text);
        c.text(font_, x, y + (h - font_.lineHeight(17)) / 2, 17, col::dark, btn, w, Align::Center);
    } else if (btn == "UD") {
        c.roundRect(x, y, w, h, 9.f, col::text);
        drawArrow(c, x + 13.f, y + 14.f, 5.f, 0, col::dark);
        drawArrow(c, x + 27.f, y + 14.f, 5.f, 1, col::dark);
    } else if (btn == "LR") {
        c.roundRect(x, y, w, h, 9.f, col::text);
        drawArrow(c, x + 13.f, y + 14.f, 5.f, 2, col::dark);
        drawArrow(c, x + 27.f, y + 14.f, 5.f, 3, col::dark);
    } else {
        c.roundRect(x, y, w, h, 9.f, col::text);
        c.text(font_, x, y + (h - font_.lineHeight(15)) / 2, 15, col::dark, btn, w, Align::Center);
    }
    return w;
}

void App::drawFooter(Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints,
                     const std::string& leftNote) {
    int x = kW - 26;
    const int y = kFooterY + 12;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        const int tw = font_.measure(it->second, 18);
        const int right = x;
        x -= tw;
        c.text(font_, x, y + 2, 18, col::text, it->second);
        x -= 8 + buttonBadgeWidth(it->first);
        drawButtonBadge(c, x, y, it->first);
        if (const uint32_t b = buttonFor(it->first)) addHit(x - 6, kFooterY + 2, right - x + 12, kH - kFooterY - 4, HitNone, 0, b);
        x -= 24;
    }
    // notifications live in the footer so they never cover the list
    if (!toasts_.empty()) {
        const Toast& t = toasts_.back();
        const int w = std::min(x - 40, font_.measure(t.text, 17) + 50);
        if (w > 80) {
            c.roundRect(16, kFooterY + 8, w, 36, 18.f, hexColor(0x2A1B24), withAlpha(t.color, 230), 1.5f);
            c.circle(36.f, kFooterY + 26.f, 5.f, t.color);
            c.text(font_, 50, kFooterY + 26 - font_.lineHeight(17) / 2, 17, col::text, t.text, w - 46);
        }
    } else if (!leftNote.empty() && x > 80) {
        c.text(font_, 26, y + 3, 16, col::muted, leftNote, x - 40);
    }
}

void App::drawPanel(Canvas& c, int x, int y, int w, int h) { c.roundRect(x, y, w, h, 14.f, col::panel, col::panelBorder, 1.5f); }

void App::drawScrollbar(Canvas& c, int x, int y, int h, int total, int visible, int first) {
    if (total <= visible || total <= 0) return;
    c.roundRect(x, y, 6, h, 3.f, hexColor(0xFFFFFF, 18));
    const int th = std::max(28, h * visible / total);
    const int maxFirst = total - visible;
    const int ty = y + (h - th) * std::min(first, maxFirst) / std::max(1, maxFirst);
    c.roundRect(x, ty, 6, th, 3.f, withAlpha(col::accent, 200));
}

void App::drawRowHighlight(Canvas& c, int x, int y, int w, int h) {
    c.roundRect(x, y, w, h, 10.f, col::rowSel, col::accent, 2.f);
}

void App::drawToggle(Canvas& c, int x, int y, bool on) {
    c.roundRect(x, y, 52, 28, 14.f, on ? col::accent : col::toggleOff);
    c.circle(on ? x + 38.f : x + 14.f, y + 14.f, 10.5f, on ? col::text : col::dim);
}

void App::drawIconFolder(Canvas& c, int x, int y, uint32_t color) {
    c.roundRect(x, y + 2, 14, 8, 3.f, color);
    c.roundRect(x, y + 6, 30, 22, 4.f, color);
    c.roundRect(x + 2, y + 10, 26, 16, 3.f, withAlpha(col::dark, 90));
}

void App::drawIconZip(Canvas& c, int x, int y, uint32_t color) {
    c.roundRect(x + 2, y, 26, 30, 4.f, color);
    for (int i = 0; i < 4; ++i) c.fillRect(x + 13 + (i % 2) * 3, y + 3 + i * 4, 3, 3, col::dark);
    c.roundRect(x + 11, y + 19, 8, 7, 2.f, col::dark);
}

void App::drawIconFile(Canvas& c, int x, int y, uint32_t color) {
    const float pts[10] = {x + 3.f, y + 0.f, x + 19.f, y + 0.f, x + 27.f, y + 8.f, x + 27.f, y + 30.f, x + 3.f, y + 30.f};
    c.polygon(pts, 5, color);
    const float fold[6] = {x + 19.f, y + 0.f, x + 27.f, y + 8.f, x + 19.f, y + 8.f};
    c.polygon(fold, 3, withAlpha(col::dark, 120));
    for (int i = 0; i < 3; ++i) c.fillRect(x + 8, y + 14 + i * 5, 14, 2, withAlpha(col::dark, 140));
}

void App::drawIconConsole(Canvas& c, int x, int y) {
    // a tiny handheld console: red and blue controllers around a dark screen
    c.roundRect(x - 2, y + 4, 10, 24, 5.f, col::accent);
    c.roundRect(x + 24, y + 4, 10, 24, 5.f, col::joyBlue);
    c.roundRect(x + 6, y + 4, 20, 24, 2.f, col::text);
    c.roundRect(x + 8, y + 7, 16, 18, 2.f, col::dark);
    c.circle(x + 3.f, y + 11.f, 2.f, col::dark);
    c.circle(x + 29.f, y + 21.f, 2.f, col::dark);
}

void App::drawIconShield(Canvas& c, int x, int y, uint32_t color) {
    const float pts[10] = {x + 15.f, y + 0.f, x + 29.f, y + 5.f, x + 26.f, y + 22.f, x + 15.f, y + 30.f, x + 4.f, y + 22.f};
    const float pts2[6] = {x + 15.f, y + 0.f, x + 4.f, y + 22.f, x + 1.f, y + 5.f};
    c.polygon(pts, 5, color);
    c.polygon(pts2, 3, color);
    c.line(x + 9.f, y + 15.f, x + 14.f, y + 20.f, 2.5f, col::dark);
    c.line(x + 14.f, y + 20.f, x + 22.f, y + 9.f, 2.5f, col::dark);
}

void App::drawWarnBadge(Canvas& c, int cx, int cy) {
    c.circle(static_cast<float>(cx), static_cast<float>(cy), 9.f, col::warn);
    c.text(font_, cx - 9, cy - font_.lineHeight(14) / 2, 14, col::dark, "!", 18, Align::Center);
}

int App::drawTag(Canvas& c, int x, int y, const std::string& text, uint32_t color) {
    const int w = font_.measure(text, 13) + 14;
    c.roundRect(x, y, w, 20, 6.f, withAlpha(color, 40), withAlpha(color, 200), 1.f);
    c.text(font_, x, y + (20 - font_.lineHeight(13)) / 2, 13, color, text, w, Align::Center);
    return w;
}

void App::drawEmptyState(Canvas& c, int x, int y, int w, int h, const std::string& title, const std::string& text) {
    const int ty = y + h / 2 - 70;
    c.text(font_, x + 40, ty, 24, col::text, title, w - 80, Align::Center);
    const std::vector<std::string> lines = wrapText(font_, text, 18, w - 120);
    int ly = ty + 44;
    for (const std::string& l : lines) {
        c.text(font_, x + 60, ly, 18, col::dim, l, w - 120, Align::Center);
        ly += font_.lineHeight(18) + 4;
    }
}

// =====================================================================================
// Drawing - screens
// =====================================================================================

void App::drawScreen(Canvas& c) {
    drawBackground(c);
    switch (screen_) {
        case Screen::Titles: drawTitles(c); break;
        case Screen::Backups: drawBackups(c); break;
        case Screen::Files: drawFiles(c); break;
        case Screen::Editor:
            if (doc_) drawEditor(c);
            break;
    }
}

uint64_t App::overlaySignature() const {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    mix(modals_.size());
    mix(helpVisible_ ? 1 : 0);
    mix(busyTask_ ? 1 : 0);
    mix(static_cast<uint64_t>(screen_));
    mix(static_cast<uint64_t>(tab_));
    mix(editCounter_);
    mix(toasts_.size());
    mix(i18n::english() ? 1 : 0);
    return h | 1;  // never 0 (0 = no cached base)
}

void App::draw(Canvas& c) {
    hits_.clear();
    areas_.clear();
    const bool overlay = !modals_.empty() || helpVisible_ || busyTask_;
    if (!overlay) {
        overlaySig_ = 0;
        drawScreen(c);
    } else {
        const uint64_t sig = overlaySignature();
        const size_t n = static_cast<size_t>(kW) * kH;
        if (sig != overlaySig_ || overlayBase_.size() != n || c.width() != kW || c.height() != kH) {
            drawScreen(c);
            c.darken(0, 0, kW, kH, busyTask_ ? 4 : 3);
            overlayBase_.resize(n);
            for (int y = 0; y < kH && y < c.height(); ++y)
                std::memcpy(overlayBase_.data() + static_cast<size_t>(y) * kW,
                            c.pixels() + static_cast<size_t>(y) * c.stride(), sizeof(uint32_t) * kW);
            overlaySig_ = sig;
        } else {
            for (int y = 0; y < kH; ++y)
                std::memcpy(c.pixels() + static_cast<size_t>(y) * c.stride(),
                            overlayBase_.data() + static_cast<size_t>(y) * kW, sizeof(uint32_t) * kW);
        }
        // the screen below a dialog does not react to touches
        hits_.clear();
        areas_.clear();
        if (!modals_.empty()) drawModal(c);
        if (helpVisible_) drawHelp(c);
        if (busyTask_) {
            hits_.clear();
            areas_.clear();
            drawBusy(c);
            ++busyFrames_;
        }
    }
    redraw_ = false;
    lastDraw_ = now_;
}

void App::drawTabBar(Canvas& c, const std::vector<std::string>& labels, int active) {
    int x = kListX + 14;
    const int y = kBrowseY + 9;
    if (labels.size() > 1) {
        const int lw = drawButtonBadge(c, x, y + 4, "L");
        addHit(x - 4, y, lw + 8, 34, HitNone, 0, plat::BTN_L);
        x += lw + 10;
    }
    for (size_t i = 0; i < labels.size(); ++i) {
        const int w = font_.measure(labels[i], 18) + 34;
        const bool on = static_cast<int>(i) == active;
        if (on) c.roundRect(x, y, w, 34, 17.f, col::accent);
        else c.roundRect(x, y, w, 34, 17.f, col::panel2, col::panelBorder, 1.f);
        c.text(font_, x, y + (34 - font_.lineHeight(18)) / 2, 18, on ? col::text : col::dim, labels[i], w, Align::Center);
        addHit(x, y, w, 34, HitTab, static_cast<int>(i));
        x += w + 8;
    }
    if (labels.size() > 1) {
        const int rw = drawButtonBadge(c, x, y + 4, "R");
        addHit(x - 4, y, rw + 8, 34, HitNone, 0, plat::BTN_R);
    }
    c.fillRect(kListX + 16, kBrowseY + kBrowseTabsH - 2, kListW - 32, 1, col::panelBorder);
}

void App::drawGameIcon(Canvas& c, int x, int y, int size, const jksv::TitleDir& t) {
    if (t.isSafety) {
        drawIconShield(c, x + (size - 30) / 2, y + (size - 30) / 2, col::gold);
        return;
    }
    if (t.icon >= 0 && static_cast<size_t>(t.icon) < icons_.size()) {
        Icon& ic = icons_[static_cast<size_t>(t.icon)];
        if (!ic.tried) {
            ic.tried = true;
            if (!decodeIcon(ic.jpeg, size, ic.px)) ic.px.clear();
            std::vector<uint8_t>().swap(ic.jpeg);
        }
        if (ic.px.size() == static_cast<size_t>(size) * static_cast<size_t>(size)) {
            c.image(x, y, size, size, ic.px.data());
            return;
        }
    }
    if (!t.titleId) {
        drawIconFolder(c, x + (size - 30) / 2, y + (size - 30) / 2 - 2, t.isSilksong ? col::accent : col::dim);
        return;
    }
    // no icon: a tile with the first letter of the name
    c.roundRect(x, y, size, size, 10.f, t.isSilksong ? col::accentDim : col::panel2, col::panelBorder, 1.f);
    std::string letter = t.name.empty() ? std::string("?") : t.name.substr(0, 1);
    c.text(font_, x, y + (size - font_.lineHeight(24)) / 2, 24, col::text, letter, size, Align::Center);
}

void App::drawTitles(Canvas& c) {
    drawHeader(c, "Switch Mod Hub",
               homeTab_ == 0 ? std::string(tr("Gry, zapisy i mody", "Games, saves and mods"))
                             : tr("Mody online – github.com/", "Online mods – github.com/") + std::string(repo::kRepo));
    drawSteps(c, 0);
    std::string modsTab = tr("Mody online", "Online mods");
    if (!repo_.mods.empty()) modsTab += " (" + std::to_string(repo_.mods.size()) + ")";
    drawTabBar(c, {tr("Gry", "Games"), modsTab}, homeTab_);
    if (homeTab_ == 1) drawRepo(c);
    else drawGames(c);
}

void App::drawGames(Canvas& c) {
    const int listY = kBrowseY + kBrowseTabsH;
    const int listH = kBrowseH - kBrowseTabsH;
    if (titles_.empty()) {
        drawEmptyState(c, kListX, listY, kListW, listH, tr("Nie znaleziono gier", "No games found"),
                       tr("Na konsoli nie ma gier z zapisami ani kopii JKSV. Zagraj w grę albo zrób kopię w JKSV, potem naciśnij Y.",
                          "There are no games with saves and no JKSV backups. Play a game or make a JKSV backup, then press Y."));
    } else {
        const int visible = (listH - 16) / kBrowseRowH;
        addArea(kListX, listY, kListW, listH, HitTitles, kBrowseRowH, visible, static_cast<int>(titles_.size()));
        for (int i = titleList_.scroll; i < static_cast<int>(titles_.size()) && i < titleList_.scroll + visible; ++i) {
            const jksv::TitleDir& t = titles_[static_cast<size_t>(i)];
            const int y = listY + 8 + (i - titleList_.scroll) * kBrowseRowH;
            if (i == titleList_.sel) drawRowHighlight(c, kListX + 8, y, kListW - 28, kBrowseRowH - 6);
            addHit(kListX + 8, y, kListW - 28, kBrowseRowH - 6, HitTitles, i);
            drawGameIcon(c, kListX + 20, y + 6, 48, t);
            const int nx = kListX + 82;
            c.text(font_, nx, y + 7, 22, col::text, titleLabel(t), kListW - 270);
            std::string sub;
            auto add = [&sub](const std::string& part) { sub += (sub.empty() ? "" : "  ·  ") + part; };
            if (t.isSafety) {
                add(i18n::plural(t.backupCount, "kopia", "kopie", "kopii", "copy", "copies"));
                add(tr("tworzone automatycznie", "created automatically"));
            } else {
                if (t.liveCount > 0) add(i18n::plural(t.liveCount, "zapis w grze", "zapisy w grze", "zapisów w grze", "game save", "game saves"));
                if (t.exists) add(i18n::plural(t.backupCount, "kopia JKSV", "kopie JKSV", "kopii JKSV", "JKSV backup", "JKSV backups"));
                if (t.modsInstalled > 0)
                    add(i18n::plural(t.modsInstalled, "mod", "mody", "modów", "mod", "mods") + " (" + std::to_string(t.modsEnabled) +
                        tr(" wł.)", " on)"));
                if (sub.empty()) add(t.titleId ? std::string(tr("brak zapisów i modów", "no saves or mods")) : mods::titleHex(t.titleId));
            }
            c.text(font_, nx, y + 36, 16, col::dim, sub, kListW - 270);
            if (t.isSilksong) drawTag(c, kListX + kListW - 160, y + 21, "SILKSONG", col::gold);
            else if (t.isSafety) drawTag(c, kListX + kListW - 160, y + 21, tr("EDYTOR", "EDITOR"), col::dim);
            else if (t.modsEnabled > 0) drawTag(c, kListX + kListW - 160, y + 21, tr("MODY WŁ.", "MODS ON"), col::ok);
        }
        drawScrollbar(c, kListX + kListW - 14, listY + 8, listH - 20, static_cast<int>(titles_.size()), visible, titleList_.scroll);
    }
    int y = kBrowseY + 22;
    c.text(font_, kInfoX + 22, y, 24, col::gold, tr("Jak to działa", "How it works"));
    y += 46;
    const char* steps[] = {
        tr("Wybierz grę. Karta Zapisy: edycja zapisu w grze i kopii JKSV.", "Pick a game. Saves tab: edit the game save and JKSV backups."),
        tr("Karta Mody: włączaj i wyłączaj mody jednym przyciskiem.", "Mods tab: turn mods on and off with one button."),
        tr("Mody online (R): przeglądaj i szukaj modów w repozytorium.", "Online mods (R): browse and search mods in the repository."),
        tr("W trakcie gry mody przełączysz w nakładce Tesla / Ultrahand.", "In the game, switch mods in the Tesla / Ultrahand overlay."),
    };
    for (int i = 0; i < 4; ++i) {
        c.circle(kInfoX + 36.f, y + 12.f, 13.f, col::accentDim);
        c.text(font_, kInfoX + 23, y + 1, 16, col::text, std::to_string(i + 1), 26, Align::Center);
        y += c.textWrapped(font_, kInfoX + 60, y, kInfoW - 84, 18, col::text, steps[i]) + 14;
    }
    y += 8;
    c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 16, col::muted,
                  tr("Folder JKSV: ", "JKSV folder: ") + displaySd(workDir_) +
                      (workDirNote_.empty() ? "" : " (" + workDirNote_ + ")") +
                      tr(". Język, kopie bezpieczeństwa, aktualizacje: X (Menu).", ". Language, safety copies, updates: X (Menu)."));
    drawFooter(c, {{"A", tr("Otwórz", "Open")}, {"X", "Menu"}, {"Y", tr("Odśwież", "Refresh")}, {"-", tr("Pomoc", "Help")}, {"+", tr("Wyjście", "Exit")}},
               tr("Wybierz grę", "Pick a game"));
}

void App::drawBackups(Canvas& c) {
    drawHeader(c, titleLabel(title_),
               title_.exists ? displaySd(title_.path)
                             : (title_.titleId ? mods::titleHex(title_.titleId) + "  ·  " + tr("brak folderu w JKSV", "no JKSV folder")
                                               : std::string()));
    drawSteps(c, 1);
    if (title_.isSafety) {
        drawTabBar(c, {tr("Kopie bezpieczeństwa", "Safety copies")}, 0);
        drawSaves(c);
        return;
    }
    std::string modsTab = tr("Mody", "Mods");
    if (!gameMods_.empty()) modsTab += " (" + std::to_string(gameMods_.size()) + ")";
    drawTabBar(c, {tr("Zapisy", "Saves"), modsTab}, gameTab_);
    if (gameTab_ == 1) drawGameMods(c);
    else drawSaves(c);
}

void App::drawSaves(Canvas& c) {
    const int listY = kBrowseY + kBrowseTabsH;
    const int listH = kBrowseH - kBrowseTabsH;
    if (backups_.empty()) {
        if (title_.isSafety)
            drawEmptyState(c, kListX, listY, kListW, listH, tr("Brak kopii bezpieczeństwa", "No safety copies"),
                           tr("Kopie pojawią się tu po nadpisaniu kopii JKSV albo po zapisie w grze.",
                              "Copies appear here after overwriting a JKSV backup or saving to the game."));
        else
            drawEmptyState(c, kListX, listY, kListW, listH, tr("Brak zapisów", "No saves"),
                           tr("Ta gra nie ma zapisu na konsoli ani kopii JKSV. Zagraj i zapisz grę albo zrób kopię w JKSV, potem naciśnij Y.",
                              "This game has no save on the console and no JKSV backup. Play and save, or make a JKSV backup, then press Y."));
    } else {
        const int visible = (listH - 16) / kBrowseRowH;
        addArea(kListX, listY, kListW, listH, HitBackups, kBrowseRowH, visible, static_cast<int>(backups_.size()));
        for (int i = backupList_.scroll; i < static_cast<int>(backups_.size()) && i < backupList_.scroll + visible; ++i) {
            const jksv::Backup& b = backups_[static_cast<size_t>(i)];
            const int y = listY + 8 + (i - backupList_.scroll) * kBrowseRowH;
            if (i == backupList_.sel) drawRowHighlight(c, kListX + 8, y, kListW - 28, kBrowseRowH - 6);
            addHit(kListX + 8, y, kListW - 28, kBrowseRowH - 6, HitBackups, i);
            if (b.isLive) {
                drawIconConsole(c, kListX + 25, y + 15);
                c.text(font_, kListX + 74, y + 7, 21, col::text, backupLabel(b), kListW - 250);
                c.text(font_, kListX + 74, y + 36, 16, col::dim,
                       tr("Bezpośrednio na konsoli  ·  bez JKSV", "Directly on the console  ·  no JKSV"));
                drawTag(c, kListX + kListW - 160, y + 21, tr("W GRZE", "IN GAME"), col::accent);
                continue;
            }
            if (b.isZip) drawIconZip(c, kListX + 26, y + 15, col::gold);
            else drawIconFolder(c, kListX + 26, y + 16, col::gold);
            const bool edited = b.name.find(" - EDIT ") != std::string::npos;
            c.text(font_, kListX + 74, y + 7, 21, col::text, b.name, kListW - (edited ? 250 : 120));
            std::string sub = b.isZip ? "ZIP" : tr("Folder", "Folder");
            if (b.isZip) sub += "  ·  " + formatSize(b.size);
            if (!b.when.empty()) sub += "  ·  " + b.when;
            c.text(font_, kListX + 74, y + 36, 16, col::dim, sub);
            if (edited) drawTag(c, kListX + kListW - 160, y + 21, tr("EDYTOWANA", "EDITED"), col::gold);
        }
        drawScrollbar(c, kListX + kListW - 14, listY + 8, listH - 20, static_cast<int>(backups_.size()), visible, backupList_.scroll);
    }
    int y = kBrowseY + 22;
    const jksv::Backup* sel = backups_.empty() ? nullptr : &backups_[static_cast<size_t>(backupList_.sel)];
    if (sel && sel->isLive) {
        c.text(font_, kInfoX + 22, y, 24, col::gold, tr("Zapis w grze", "Game save"));
        y += 46;
        c.text(font_, kInfoX + 22, y, 18, col::text, tr("Użytkownik: ", "User: ") + sel->userName, kInfoW - 44);
        y += 34;
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 17, col::dim,
                           tr("Edytujesz zapis prosto na konsoli – bez JKSV. Po zapisaniu (+) zmiany są od razu w grze.",
                              "You edit the save right on the console - no JKSV. After saving (+) the changes are in the game.")) +
             14;
        c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 16, col::muted,
                      tr("Przed każdym zapisem cały zapis gry jest kopiowany do ", "Before every save the whole game save is copied to ") +
                          std::string(kSafetyDirSd) + tr(". Gra musi być zamknięta.", ". The game must be closed."));
    } else {
        c.text(font_, kInfoX + 22, y, 24, col::gold, title_.isSafety ? tr("Kopia bezpieczeństwa", "Safety copy") : tr("Kopia zapasowa", "Backup"));
        y += 46;
        if (sel) {
            y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 20, col::text, sel->name, 3) + 10;
            c.text(font_, kInfoX + 22, y, 17, col::dim, tr("Typ: ", "Type: ") + std::string(sel->isZip ? tr("archiwum ZIP", "ZIP archive") : tr("folder", "folder")));
            y += 28;
            if (sel->isZip) {
                c.text(font_, kInfoX + 22, y, 17, col::dim, tr("Rozmiar: ", "Size: ") + formatSize(sel->size));
                y += 28;
            }
            if (!sel->when.empty()) {
                c.text(font_, kInfoX + 22, y, 17, col::dim, tr("Data: ", "Date: ") + sel->when);
                y += 28;
            }
            y += 10;
        }
        std::string note;
        if (title_.isSafety) {
            const int keep = settings::get().keepBackups;
            note = tr("Tworzone przed nadpisaniem kopii JKSV i przed zapisem w grze. Otwórz kopię, aby ją obejrzeć albo wgrać "
                      "do gry (+). Zachowywane: ",
                      "Made before overwriting a JKSV backup and before saving to the game. Open a copy to view it or "
                      "write it to the game (+). Kept: ") +
                   (keep > 0 ? std::to_string(keep) + tr(" najnowszych (X: Menu).", " newest (X: Menu).") : std::string(tr("wszystkie (X: Menu).", "all (X: Menu).")));
        } else {
            note = tr("Kopie oznaczone EDYTOWANA zostały utworzone przez ten edytor. Oryginalne kopie JKSV nie są zmieniane, "
                      "chyba że wybierzesz „Nadpisz”.",
                      "Backups marked EDITED were created by this editor. Original JKSV backups are not changed unless you "
                      "pick \"Overwrite\".");
            if (liveUsers_.empty() && !liveError_.empty())
                note += std::string("\n\n") + tr("Zapis w grze niedostępny: ", "Game save unavailable: ") + liveError_;
        }
        c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 16, col::muted, note);
    }
    drawFooter(c, {{"A", tr("Otwórz", "Open")}, {"B", tr("Wstecz", "Back")}, {"X", "Menu"}, {"Y", tr("Odśwież", "Refresh")}, {"-", tr("Pomoc", "Help")}},
               tr("Wybierz zapis", "Pick a save"));
}

void App::drawFiles(Canvas& c) {
    drawHeader(c, backupLabel(backup_), titleLabel(title_));
    drawSteps(c, 2);
    if (files_.empty()) {
        drawEmptyState(c, kListX, kBrowseY, kListW, kBrowseH, tr("Brak plików zapisu", "No save files"),
                       tr("Tu nie ma plików zapisu. Wybierz inną kopię.", "There are no save files here. Pick another backup."));
    } else {
        const int visible = (kBrowseH - 16) / kBrowseRowH;
        addArea(kListX, kBrowseY, kListW, kBrowseH, HitFiles, kBrowseRowH, visible, static_cast<int>(files_.size()));
        for (int i = fileList_.scroll; i < static_cast<int>(files_.size()) && i < fileList_.scroll + visible; ++i) {
            const jksv::SaveEntry& f = files_[static_cast<size_t>(i)];
            const silk::SlotPreview& p = previews_[static_cast<size_t>(i)];
            const int y = kBrowseY + 8 + (i - fileList_.scroll) * kBrowseRowH;
            if (i == fileList_.sel) drawRowHighlight(c, kListX + 8, y, kListW - 28, kBrowseRowH - 6);
            addHit(kListX + 8, y, kListW - 28, kBrowseRowH - 6, HitFiles, i);
            drawIconFile(c, kListX + 26, y + 15, f.slot ? col::accent : col::dim);
            c.text(font_, kListX + 74, y + 7, 22, col::text, f.name, kListW - 140);
            std::string sub = (f.slot ? tr("Slot ", "Slot ") + std::to_string(f.slot) : std::string(tr("Plik", "File"))) + "  ·  " +
                              formatSize(f.size);
            if (p.ok) {
                if (p.playTime >= 0) sub += "  ·  " + formatHours(p.playTime);
                if (p.completion >= 0) sub += "  ·  " + std::to_string(p.completion) + "%";
                if (p.maxHealth >= 0) sub += "  ·  " + i18n::plural(p.maxHealth, "maska", "maski", "masek", "mask", "masks");
            }
            c.text(font_, kListX + 74, y + 36, 16, col::dim, sub, kListW - 140);
            if (p.ok && p.permadeath > 0) drawTag(c, kListX + kListW - 150, y + 21, "STEEL SOUL", p.permadeath == 2 ? col::err : col::gold);
        }
        drawScrollbar(c, kListX + kListW - 14, kBrowseY + 12, kBrowseH - 24, static_cast<int>(files_.size()),
                      visible, fileList_.scroll);
    }
    int y = kBrowseY + 22;
    const silk::SlotPreview* p = files_.empty() ? nullptr : &previews_[static_cast<size_t>(fileList_.sel)];
    if (p && p->ok) {
        const jksv::SaveEntry& f = files_[static_cast<size_t>(fileList_.sel)];
        c.text(font_, kInfoX + 22, y, 24, col::gold, f.slot ? tr("Slot ", "Slot ") + std::to_string(f.slot) : f.name);
        y += 46;
        auto line = [&](const std::string& label, const std::string& value) {
            if (value.empty()) return;
            c.text(font_, kInfoX + 22, y, 17, col::dim, label);
            c.text(font_, kInfoX + 170, y, 17, col::text, value, kInfoW - 190);
            y += 28;
        };
        line(tr("Czas gry", "Play time"), p->playTime >= 0 ? formatHours(p->playTime) : "");
        line(tr("Ukończenie", "Completion"), p->completion >= 0 ? std::to_string(p->completion) + "%" : "");
        line(tr("Maski", "Masks"), p->maxHealth >= 0 ? std::to_string(p->maxHealth) : "");
        line(tr("Jedwab", "Silk"), p->silkMax >= 0 ? std::to_string(p->silkMax) : "");
        line(tr("Różańce", "Rosaries"), p->geo >= 0 ? std::to_string(p->geo) : "");
        line(tr("Odłamki", "Shards"), p->shards >= 0 ? std::to_string(p->shards) : "");
        if (p->permadeath > 0)
            line(tr("Tryb", "Mode"), p->permadeath == 2 ? tr("Steel Soul (zniszczony)", "Steel Soul (shattered)") : "Steel Soul");
        line(tr("Ławka", "Bench"), p->scene);
        line(tr("Wersja gry", "Game version"), p->version);
    } else {
        c.text(font_, kInfoX + 22, y, 24, col::gold, tr("Plik zapisu", "Save file"));
        y += 46;
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 18, col::text,
                           tr("userN.dat to slot zapisu N w menu gry (1–4). Pozostałe pliki to kopie i punkty przywracania gry.",
                              "userN.dat is save slot N in the game menu (1-4). Other files are the game's backups and restore points.")) +
             14;
        c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 16, col::muted,
                      tr("Format zapisu (JSON ze Switcha lub zaszyfrowany zapis PC) jest wykrywany automatycznie i zachowywany przy zapisie.",
                         "The save format (Switch JSON or an encrypted PC save) is detected automatically and kept when saving."));
    }
    drawFooter(c, {{"A", tr("Edytuj", "Edit")}, {"B", tr("Wstecz", "Back")}, {"X", "Menu"}, {"-", tr("Pomoc", "Help")}},
               tr("Wybierz slot", "Pick a slot"));
}

void App::drawEditor(Canvas& c) {
    const std::string title = doc_->entry.name + (doc_->entry.slot ? "  –  Slot " + std::to_string(doc_->entry.slot) : "");
    drawHeader(c, title, backupLabel(doc_->backup) + "  ·  " +
                             (doc_->binary ? std::string(tr("plik binarny", "binary file")) : std::string(saveFormatName(doc_->codec.format))));
    drawSteps(c, 3);

    // tabs
    const int count = changeCount();
    int x = kListX;
    const int ty = 94;
    const int lw = drawButtonBadge(c, x, ty + 5, "L");
    addHit(x - 4, ty, lw + 8, 38, HitNone, 0, plat::BTN_L);
    x += lw + 10;
    for (int t : editorTabs()) {
        std::string label;
        switch (t) {
            case TabQuick: label = tr("Szybka edycja", "Quick edit"); break;
            case TabTree: label = tr("Wszystkie dane", "All data"); break;
            case TabActions: label = tr("Akcje", "Actions"); break;
            case TabHex: label = tr("Bajty (HEX)", "Bytes (hex)"); break;
            default: label = tr("Zmiany", "Changes") + (count > 0 ? " (" + std::to_string(count) + ")" : std::string()); break;
        }
        const int w = font_.measure(label, 19) + 36;
        if (t == tab_) c.roundRect(x, ty, w, 38, 19.f, col::accent);
        else c.roundRect(x, ty, w, 38, 19.f, col::panel2, col::panelBorder, 1.f);
        c.text(font_, x, ty + (38 - font_.lineHeight(19)) / 2, 19, t == tab_ ? col::text : col::dim, label, w, Align::Center);
        addHit(x, ty, w, 38, HitTab, t);
        x += w + 8;
    }
    const int rw = drawButtonBadge(c, x, ty + 5, "R");
    addHit(x - 4, ty, rw + 8, 38, HitNone, 0, plat::BTN_R);

    // change counter
    const bool dirty = count > 0;
    c.roundRect(kInfoX, ty, kInfoW, 38, 19.f, dirty ? withAlpha(col::warn, 40) : col::panel2,
                dirty ? withAlpha(col::warn, 180) : col::panelBorder, 1.f);
    const std::string status = dirty ? tr("Niezapisane zmiany: ", "Unsaved changes: ") + std::to_string(count)
                                     : std::string(tr("Brak niezapisanych zmian", "No unsaved changes"));
    c.circle(kInfoX + 22.f, ty + 19.f, 6.f, dirty ? col::warn : col::ok);
    c.text(font_, kInfoX + 38, ty + (38 - font_.lineHeight(17)) / 2, 17, dirty ? col::warn : col::dim, status,
           kInfoW - 50);
    addHit(kInfoX, ty, kInfoW, 38, HitTab, TabChanges);

    switch (tab_) {
        case TabQuick: drawQuickTab(c); break;
        case TabTree: drawTreeTab(c); break;
        case TabActions: drawActionsTab(c); break;
        case TabHex: drawHexTab(c); break;
        default: drawChangesTab(c); break;
    }
}

void App::drawValueDetails(Canvas& c, int x, int y, int w, int h, const std::string& title, const std::string& keyPath,
                           const json::Value* v, const std::string& help, const std::string& controls,
                           const std::string& valueText, bool valueModified, const std::string& warning) {
    const int ix = x + 22, iw = w - 44;
    int cy = y + 18;
    cy += c.textWrapped(font_, ix, cy, iw, 23, col::text, title, 2) + 2;
    if (!keyPath.empty()) cy += c.textWrapped(font_, ix, cy, iw, 14, col::muted, keyPath, 2) + 10;
    std::string val = valueText;
    bool mod = valueModified;
    if (v) {
        c.text(font_, ix, cy, 15, col::dim, typeName(*v), iw);
        cy += 26;
        switch (v->type) {
            case json::Type::Bool: val = v->boolean ? tr("TAK  (true)", "YES  (true)") : tr("NIE  (false)", "NO  (false)"); break;
            case json::Type::String: val = "\"" + v->text + "\""; break;
            default: val = valuePreview(*v);
        }
        mod = v->modified;
    }
    if (!val.empty()) {
        const uint32_t vc = !warning.empty() ? col::warn : (mod ? col::gold : col::text);
        const std::vector<std::string> lines = wrapText(font_, val, 22, iw - 28);
        const int nLines = std::min<int>(4, static_cast<int>(lines.size()));
        const int boxH = std::max(1, nLines) * (font_.lineHeight(22) + 2) + 20;
        c.roundRect(ix, cy, iw, boxH, 10.f, hexColor(0x000000, 70), !warning.empty() ? col::warn : (mod ? col::gold : col::panelBorder), 1.5f);
        for (int i = 0; i < nLines; ++i)
            c.text(font_, ix + 14, cy + 10 + i * (font_.lineHeight(22) + 2), 22, vc, lines[static_cast<size_t>(i)], iw - 28);
        cy += boxH + 10;
        if (v && v->modified) cy += c.textWrapped(font_, ix, cy, iw, 16, col::gold, tr("Oryginał: ", "Original: ") + json::originalDisplay(*v), 2) + 6;
    }
    if (!warning.empty()) cy += c.textWrapped(font_, ix, cy, iw, 16, col::warn, warning, 3) + 6;
    if (!help.empty()) cy += c.textWrapped(font_, ix, cy, iw, 16, col::dim, help, 6) + 6;
    if (!controls.empty()) {
        const std::vector<std::string> lines = wrapText(font_, controls, 15, iw);
        int ly = y + h - 16 - static_cast<int>(lines.size()) * (font_.lineHeight(15) + 3);
        if (ly > cy)
            for (const std::string& l : lines) {
                c.text(font_, ix, ly, 15, col::muted, l, iw);
                ly += font_.lineHeight(15) + 3;
            }
    }
}

void App::drawQuickTab(Canvas& c) {
    if (quickVersion_ != doc_->structVersion) rebuildQuickRows();
    const int visible = (kEditH - 16) / kQuickRowH;
    const int n = static_cast<int>(quickRows_.size());
    addArea(kListX, kEditY, kListW, kEditH, HitQuick, kQuickRowH, visible, n);
    for (int i = quickList_.scroll; i < n && i < quickList_.scroll + visible; ++i) {
        silk::QuickRow& r = quickRows_[static_cast<size_t>(i)];
        const int y = kEditY + 8 + (i - quickList_.scroll) * kQuickRowH;
        if (r.kind == silk::RowKind::Header) {
            c.text(font_, kListX + 22, y + 13, 17, col::gold, r.label, kListW - 60);
            const int tw = font_.measure(r.label, 17);
            c.fillRect(kListX + 34 + tw, y + 25, kListW - 80 - tw, 1, withAlpha(col::gold, 60));
            continue;
        }
        const bool sel = i == quickList_.sel;
        if (sel) drawRowHighlight(c, kListX + 8, y + 2, kListW - 28, kQuickRowH - 4);
        addHit(kListX + 8, y + 2, kListW - 28, kQuickRowH - 4, HitQuick, i);
        const bool mod = rowModified(r);
        if (mod) c.circle(kListX + 22.f, y + kQuickRowH / 2.f, 4.f, col::gold);
        c.text(font_, kListX + 34, y + (kQuickRowH - font_.lineHeight(20)) / 2, 20, col::text, r.label, kListW - 330);
        const int right = kListX + kListW - 34;
        if (r.kind == silk::RowKind::Bool) {
            drawToggle(c, right - 52, y + 8, r.value->boolean);
            continue;
        }
        std::string val;
        bool over = false;
        if (r.kind == silk::RowKind::Choice) {
            const int ch = r.getChoice();
            val = (ch >= 0 && ch < static_cast<int>(r.choices.size())) ? r.choices[static_cast<size_t>(ch)] : "?";
        } else {
            val = r.isTime ? formatDuration(json::asDouble(*r.value)) : r.value->text;
            over = r.gameMax > 0 && json::isIntegerNumber(*r.value) && json::asInt(*r.value) > r.gameMax;
        }
        const int vw = 230;
        c.text(font_, right - vw - (sel ? 22 : 0), y + (kQuickRowH - font_.lineHeight(20)) / 2, 20,
               over ? col::warn : (mod ? col::gold : col::text), val, vw, Align::Right);
        if (over)
            drawWarnBadge(c, right - (sel ? 22 : 0) - std::min(vw, font_.measure(val, 20)) - 16, y + kQuickRowH / 2);
        if (sel) {
            drawArrow(c, right - vw - 40.f, y + kQuickRowH / 2.f, 6.f, 2, col::accent);
            drawArrow(c, right - 8.f, y + kQuickRowH / 2.f, 6.f, 3, col::accent);
            addHit(right - vw - 56, y + 2, 34, kQuickRowH - 4, HitNone, 0, plat::BTN_LEFT);
            addHit(right - 24, y + 2, 34, kQuickRowH - 4, HitNone, 0, plat::BTN_RIGHT);
        }
    }
    drawScrollbar(c, kListX + kListW - 14, kEditY + 12, kEditH - 24, n, visible, quickList_.scroll);

    if (n > 0) {
        silk::QuickRow& r = quickRows_[static_cast<size_t>(quickList_.sel)];
        std::string controls;
        if (r.kind == silk::RowKind::Bool)
            controls = tr("A lub lewo/prawo: przełącz  ·  X: przywróć oryginał", "A or left/right: toggle  ·  X: restore the original");
        else if (r.kind == silk::RowKind::Choice)
            controls = tr("Lewo/prawo lub A: zmień stan  ·  X: przywróć", "Left/right or A: change the state  ·  X: restore");
        else if (r.kind != silk::RowKind::Header)
            controls = tr("Lewo/prawo: zmień (przytrzymaj = szybciej)  ·  A: wpisz wartość  ·  X: przywróć",
                          "Left/right: change (hold = faster)  ·  A: type a value  ·  X: restore");
        controls += tr("  ·  Y: następna sekcja", "  ·  Y: next section");
        std::string help = r.help;
        if (r.isTime && r.value) help += tr("\nAktualnie: ", "\nNow: ") + formatDuration(json::asDouble(*r.value)) + " (" + r.value->text + " s)";
        std::string warning;
        if (r.kind == silk::RowKind::Int && r.gameMax > 0 && json::isIntegerNumber(*r.value) && json::asInt(*r.value) > r.gameMax)
            warning = tr("Powyżej limitu gry (", "Above the game's limit (") + std::to_string(r.gameMax) +
                      tr("). Gra może działać nieprawidłowo.", "). The game may misbehave.");
        if (r.kind == silk::RowKind::Choice) {
            const int ch = r.getChoice();
            std::string state = (ch >= 0 && ch < static_cast<int>(r.choices.size())) ? r.choices[static_cast<size_t>(ch)]
                                                                                     : std::string(tr("nietypowy stan", "unusual state"));
            std::string fields;
            for (size_t k = 0; k < r.touched.size(); ++k)
                fields += (fields.empty() ? "" : "\n") +
                          (k < r.touchedNames.size() ? r.touchedNames[k] + " = " : std::string()) + saveName(*r.touched[k]);
            drawValueDetails(c, kInfoX, kEditY, kInfoW, kEditH, r.label, r.key, nullptr, help + tr("\nPola: ", "\nFields: ") + fields,
                             controls, state, rowModified(r));
        } else {
            drawValueDetails(c, kInfoX, kEditY, kInfoW, kEditH, r.label, r.key,
                             r.kind == silk::RowKind::Header ? nullptr : r.value, help, controls, "", false, warning);
        }
    }
    std::vector<std::pair<std::string, std::string>> hints = {{"LR", tr("Zmień", "Change")}, {"A", tr("Edytuj", "Edit")},
                                                              {"X", tr("Przywróć", "Restore")}, {"Y", tr("Sekcja", "Section")},
                                                              {"+", tr("Zapisz", "Save")}, {"B", tr("Wyjdź", "Leave")}};
    drawFooter(c, hints, silk::summary(doc_->root));
}

void App::drawTreeTab(Canvas& c) {
    json::Value* cont = resolveTree();
    rebuildTreeRows(cont);
    TreeLevel& lvl = tree_.back();
    // breadcrumb
    c.text(font_, kListX + 22, kEditY + 14, 16, col::dim, fitText(font_, treePathText(false), 16, kListW - 300));
    if (!lvl.filter.empty()) {
        const std::string chip = tr("Filtr: ", "Filter: ") + lvl.filter + "  (" + std::to_string(treeRows_.size()) + ")";
        const int w = std::min(260, font_.measure(chip, 14) + 20);
        c.roundRect(kListX + kListW - w - 22, kEditY + 10, w, 26, 8.f, withAlpha(col::accent, 50), col::accent, 1.f);
        c.text(font_, kListX + kListW - w - 22, kEditY + 10 + (26 - font_.lineHeight(14)) / 2, 14, col::text, chip, w,
               Align::Center);
    }
    c.fillRect(kListX + 16, kEditY + 44, kListW - 32, 1, col::panelBorder);

    const int listY = kEditY + kTreeListTop;
    const int visible = (kEditH - kTreeListTop - 8) / kTreeRowH;
    const int n = static_cast<int>(treeRows_.size());
    addArea(kListX, listY, kListW, kEditH - kTreeListTop, HitTree, kTreeRowH, visible, n);
    if (n == 0) {
        c.text(font_, kListX + 40, listY + 30, 19, col::dim,
               lvl.filter.empty() ? tr("Ten element jest pusty.", "This element is empty.")
                                  : tr("Brak wyników dla filtra. Naciśnij B, aby go wyczyścić.", "No results for the filter. Press B to clear it."));
    }
    for (int i = lvl.list.scroll; i < n && i < lvl.list.scroll + visible; ++i) {
        const size_t idx = treeRows_[static_cast<size_t>(i)];
        const json::Value* v = treeChild(cont, idx);
        const int y = listY + (i - lvl.list.scroll) * kTreeRowH;
        const bool sel = i == lvl.list.sel;
        if (sel) drawRowHighlight(c, kListX + 8, y + 1, kListW - 28, kTreeRowH - 4);
        addHit(kListX + 8, y + 1, kListW - 28, kTreeRowH - 4, HitTree, i);
        const bool mod = v->modified || (v->isContainer() && sel && json::countModified(*v) > 0);
        if (v->modified) c.circle(kListX + 22.f, y + kTreeRowH / 2.f, 4.f, col::gold);
        else if (v->added) c.circle(kListX + 22.f, y + kTreeRowH / 2.f, 4.f, col::ok);
        const int ty = y + (kTreeRowH - font_.lineHeight(19)) / 2;
        c.text(font_, kListX + 34, ty, 19, v->isContainer() ? col::gold : col::text, childLabel(*cont, idx), 430);
        const int right = kListX + kListW - 34;
        if (v->type == json::Type::Bool) {
            drawToggle(c, right - 52, y + 7, v->boolean);
        } else if (v->isContainer()) {
            c.text(font_, right - 260, ty, 17, col::dim, valuePreview(*v), 230, Align::Right);
            drawArrow(c, right - 8.f, y + kTreeRowH / 2.f, 6.f, 3, col::dim);
        } else {
            const bool over = treeLimit(*cont, idx) > 0;
            c.text(font_, right - 300, ty, 19, over ? col::warn : (mod ? col::gold : col::text), valuePreview(*v), 300, Align::Right);
        }
    }
    drawScrollbar(c, kListX + kListW - 14, listY, kEditH - kTreeListTop - 12, n, visible, lvl.list.scroll);

    if (n > 0) {
        const size_t idx = treeRows_[static_cast<size_t>(lvl.list.sel)];
        const json::Value* v = treeChild(cont, idx);
        std::string controls;
        if (v->isContainer()) controls = tr("A: otwórz  ·  X: menu  ·  B: wstecz", "A: open  ·  X: menu  ·  B: back");
        else if (v->type == json::Type::Bool) controls = tr("A lub lewo/prawo: przełącz  ·  X: menu", "A or left/right: toggle  ·  X: menu");
        else if (v->type == json::Type::Number)
            controls = tr("Lewo/prawo: zmień (przytrzymaj = szybciej)  ·  A: wpisz  ·  X: menu", "Left/right: change (hold = faster)  ·  A: type  ·  X: menu");
        else if (v->type == json::Type::String) controls = tr("A: edytuj tekst  ·  X: menu", "A: edit the text  ·  X: menu");
        std::string help;
        if (v->isContainer() && json::countModified(*v) > 0)
            help = tr("Zmienione wartości w środku: ", "Changed values inside: ") + std::to_string(json::countModified(*v));
        if (v->added) help += (help.empty() ? "" : "\n") + std::string(tr("Nowy wpis dodany w edytorze.", "New entry added in the editor."));
        const long long lim = treeLimit(*cont, idx);
        const std::string warning = lim > 0 ? tr("Powyżej limitu gry (", "Above the game's limit (") + std::to_string(lim) +
                                                  tr("). Gra może działać nieprawidłowo.", "). The game may misbehave.")
                                            : std::string();
        drawValueDetails(c, kInfoX, kEditY, kInfoW, kEditH, childLabel(*cont, idx), treePathText(true), v, help, controls, "",
                         false, warning);
    }
    drawFooter(c, {{"A", tr("Otwórz/Edytuj", "Open/Edit")}, {"X", "Menu"}, {"Y", tr("Szukaj", "Search")}, {"+", tr("Zapisz", "Save")}, {"B", tr("Wstecz", "Back")}},
               silk::summary(doc_->root));
}

void App::drawActionsTab(Canvas& c) {
    const std::vector<silk::BulkAction>& acts = silk::bulkActions();
    const std::vector<int>& counts = actionCounts();
    const int nBulk = static_cast<int>(acts.size());
    const int count = actionRowCount();
    const int visible = (kEditH - 16) / kActionRowH;
    const bool toPc = doc_->codec.format == SaveFormat::PlainJson;
    const std::string importLabel = tr("Importuj zapis z PC (zastąp ten plik)", "Import a PC save (replace this file)");
    const std::string importHelp = tr("Wczytuje plik z sdmc:/switch/SilksongSaveEditor/import i zapisuje go w formacie tego slotu "
                                      "(np. zapis PC → Switch).",
                                      "Loads a file from sdmc:/switch/SilksongSaveEditor/import and stores it in this slot's format "
                                      "(e.g. PC save → Switch).");
    const std::string exportLabel = toPc ? tr("Eksportuj jako zapis PC", "Export as a PC save") : tr("Eksportuj jako zapis Switch", "Export as a Switch save");
    const std::string exportHelp = tr("Zapisuje bieżący stan (ze zmianami) do sdmc:/switch/SilksongSaveEditor/export – do skopiowania "
                                      "na komputer.",
                                      "Writes the current state (with your changes) to sdmc:/switch/SilksongSaveEditor/export - to "
                                      "copy to a computer.");
    addArea(kListX, kEditY, kListW, kEditH, HitActions, kActionRowH, visible, count);
    for (int i = actionList_.scroll; i < count && i < actionList_.scroll + visible; ++i) {
        const int y = kEditY + 8 + (i - actionList_.scroll) * kActionRowH;
        if (i == actionList_.sel) drawRowHighlight(c, kListX + 8, y, kListW - 28, kActionRowH - 6);
        addHit(kListX + 8, y, kListW - 28, kActionRowH - 6, HitActions, i);
        if (i < nBulk) {
            const silk::BulkAction& a = acts[static_cast<size_t>(i)];
            c.text(font_, kListX + 26, y + 7, 21, col::text, a.label, kListW - 260);
            c.text(font_, kListX + 26, y + 36, 15, col::dim, fitText(font_, a.help, 15, kListW - 260));
            const int cnt = counts[static_cast<size_t>(i)];
            if (cnt > 0) drawTag(c, kListX + kListW - 190, y + 21, tr("zmieni: ", "changes: ") + std::to_string(cnt), col::gold);
            else drawTag(c, kListX + kListW - 190, y + 21, tr("już ustawione", "already set"), col::ok);
        } else if (i == nBulk || i == nBulk + 1) {
            const bool imp = i == nBulk;
            c.text(font_, kListX + 26, y + 7, 21, col::text, imp ? importLabel : exportLabel, kListW - 260);
            c.text(font_, kListX + 26, y + 36, 15, col::dim, fitText(font_, imp ? importHelp : exportHelp, 15, kListW - 260));
            drawTag(c, kListX + kListW - 190, y + 21, imp ? tr("import", "import") : tr("eksport", "export"), col::joyBlue);
        } else {
            c.text(font_, kListX + 26, y + 7, 21, col::warn, tr("Cofnij: ", "Undo: ") + doc_->undoLabel, kListW - 120);
            c.text(font_, kListX + 26, y + 36, 15, col::dim, tr("Przywraca stan sprzed ostatniej akcji.", "Restores the state from before the last action."));
        }
    }
    drawScrollbar(c, kListX + kListW - 14, kEditY + 12, kEditH - 24, count, visible, actionList_.scroll);

    int y = kEditY + 20;
    const int sel = actionList_.sel;
    if (sel < nBulk) {
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 23, col::text, acts[static_cast<size_t>(sel)].label, 2) + 8;
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 17, col::dim, acts[static_cast<size_t>(sel)].help, 8) + 12;
        const int cnt = counts[static_cast<size_t>(sel)];
        c.text(font_, kInfoX + 22, y, 17, cnt > 0 ? col::gold : col::ok,
               cnt > 0 ? tr("Zmieni: ", "Changes: ") + i18n::plural(cnt, "wpis", "wpisy", "wpisów", "entry", "entries")
                       : std::string(tr("Wszystko jest już ustawione", "Everything is already set")));
    } else if (sel == nBulk || sel == nBulk + 1) {
        const bool imp = sel == nBulk;
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 23, col::text, imp ? importLabel : exportLabel, 3) + 8;
        c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 17, col::dim,
                      imp ? importHelp + tr(" Zgodność zapisów PC i Switch nie jest gwarantowana – sprawdź zapis w grze.",
                                            " PC and Switch saves are not guaranteed to be compatible - check the save in the game.")
                          : exportHelp,
                      9);
    } else if (doc_->undoRoot) {
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 23, col::text, tr("Cofnij ostatnią akcję", "Undo the last action"), 2) + 8;
        c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 17, col::dim,
                      tr("Przywraca zapis do stanu sprzed akcji „", "Restores the save to its state before \"") + doc_->undoLabel + tr("”.", "\"."));
    }
    c.textWrapped(font_, kInfoX + 22, kEditY + kEditH - 92, kInfoW - 44, 15, col::muted,
                  tr("Akcje zmieniają tylko dane w pamięci. Do zapisu trafią dopiero po zapisaniu (+).",
                     "Actions only change the data in memory. Nothing is written before you save (+)."));
    drawFooter(c, {{"UD", tr("Wybierz", "Select")}, {"A", tr("Wykonaj", "Run")}, {"+", tr("Zapisz", "Save")}, {"B", tr("Wyjdź", "Leave")}},
               silk::summary(doc_->root));
}

void App::drawChangesTab(Canvas& c) {
    const std::vector<Change>& list = changes();
    const int n = static_cast<int>(list.size());
    const int visible = (kEditH - 16) / kChangeRowH;
    addArea(kListX, kEditY, kListW, kEditH, HitChanges, kChangeRowH, visible, n);
    if (n == 0) {
        drawEmptyState(c, kListX, kEditY, kListW, kEditH, tr("Brak zmian", "No changes"),
                       tr("Tu pojawi się każda zmieniona wartość: co było, a co jest teraz.",
                          "Every changed value shows up here: what it was and what it is now."));
    }
    for (int i = changeList_.scroll; i < n && i < changeList_.scroll + visible; ++i) {
        const Change& ch = list[static_cast<size_t>(i)];
        const int y = kEditY + 8 + (i - changeList_.scroll) * kChangeRowH;
        if (i == changeList_.sel) drawRowHighlight(c, kListX + 8, y, kListW - 28, kChangeRowH - 4);
        addHit(kListX + 8, y, kListW - 28, kChangeRowH - 4, HitChanges, i);
        uint32_t dot = col::gold;
        const char* sym = "";
        switch (ch.kind) {
            case Change::Value: dot = ch.limit ? col::warn : col::gold; break;
            case Change::Added: dot = col::ok; sym = "+"; break;
            case Change::Removed: dot = col::err; sym = "−"; break;
            case Change::Imported: dot = col::joyBlue; break;
            case Change::Bytes: dot = col::gold; break;
        }
        const float my = y + kChangeRowH / 2.f - 2.f;
        c.circle(kListX + 30.f, my, 11.f, withAlpha(dot, 60));
        if (*sym)
            c.text(font_, kListX + 19, static_cast<int>(my) - font_.lineHeight(18) / 2 - 1, 18, dot, sym, 22, Align::Center);
        else
            c.circle(kListX + 30.f, my, 4.5f, dot);
        c.text(font_, kListX + 50, y + 6, 18, col::text, fitText(font_, ch.path, 18, kListW - 100));
        std::string detail = (ch.kind == Change::Value || ch.kind == Change::Bytes) ? ch.before + "  →  " + ch.after : ch.after;
        c.text(font_, kListX + 50, y + 31, 15, ch.limit ? col::warn : col::dim, fitText(font_, detail, 15, kListW - 230));
        if (ch.limit) drawTag(c, kListX + kListW - 190, y + 28, tr("powyżej limitu gry", "above the game limit"), col::warn);
    }
    drawScrollbar(c, kListX + kListW - 14, kEditY + 12, kEditH - 24, n, visible, changeList_.scroll);

    if (n > 0) {
        const Change& ch = list[static_cast<size_t>(changeList_.sel)];
        int y = kEditY + 18;
        const char* kind = tr("Zmieniona wartość", "Changed value");
        if (ch.kind == Change::Added) kind = tr("Dodany wpis", "Added entry");
        else if (ch.kind == Change::Removed) kind = tr("Usunięty wpis", "Removed entry");
        else if (ch.kind == Change::Imported) kind = tr("Import zapisu", "Save import");
        else if (ch.kind == Change::Bytes) kind = tr("Zmienione bajty", "Changed bytes");
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 23, col::text, kind, 2) + 4;
        y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 15, col::muted, ch.path, 4) + 12;
        if (ch.kind == Change::Value || ch.kind == Change::Bytes) {
            c.text(font_, kInfoX + 22, y, 16, col::dim, tr("Było", "Before"));
            y += 24;
            y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 20, col::text, ch.before, 3) + 10;
            c.text(font_, kInfoX + 22, y, 16, col::dim, tr("Jest", "Now"));
            y += 24;
            y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 20, ch.limit ? col::warn : col::gold, ch.after, 3) + 10;
            if (ch.limit)
                y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 16, col::warn,
                                   tr("Powyżej limitu gry (", "Above the game's limit (") + std::to_string(ch.limit) + ").", 2) + 6;
        } else {
            y += c.textWrapped(font_, kInfoX + 22, y, kInfoW - 44, 18, col::gold, ch.after, 3) + 10;
        }
        std::string controls = tr("A: pokaż w „Wszystkie dane”  ·  X: cofnij tę zmianę", "A: show in \"All data\"  ·  X: undo this change");
        if (ch.kind == Change::Bytes)
            controls = tr("A: pokaż w edytorze HEX  ·  X: cofnij tę zmianę", "A: show in the hex editor  ·  X: undo this change");
        else if (ch.kind == Change::Imported)
            controls = tr("Cofniesz w karcie Akcje („Cofnij”) albo przez + → „Odrzuć wszystkie zmiany”",
                          "Undo it on the Actions tab (\"Undo\") or with + → \"Discard all changes\"");
        else if (ch.kind == Change::Removed)
            controls = tr("Cofniesz przez + → „Odrzuć wszystkie zmiany”", "Undo it with + → \"Discard all changes\"");
        c.textWrapped(font_, kInfoX + 22, kEditY + kEditH - 60, kInfoW - 44, 15, col::muted, controls, 3);
    }
    drawFooter(c, {{"A", tr("Pokaż", "Show")}, {"X", tr("Cofnij", "Undo")}, {"+", tr("Zapisz", "Save")}, {"B", tr("Wyjdź", "Leave")}},
               silk::summary(doc_->root));
}

// =====================================================================================
// Drawing - overlays
// =====================================================================================

void App::drawModal(Canvas& c) {
    Modal& m = modals_.back();
    const int w = 700;
    const std::vector<std::string> lines = wrapText(font_, m.text, 18, w - 64);
    const int lh = font_.lineHeight(18) + 4;
    int textLines = static_cast<int>(lines.size());
    const int maxH = kH - 40;
    if (!m.items.empty()) textLines = std::min(textLines, 9);
    int h = 78 + textLines * lh + 20;
    const int itemsAvail = std::max(kModalItemH, maxH - h - 12);
    const int visibleItems = m.items.empty() ? 0 : std::max(1, std::min(static_cast<int>(m.items.size()), itemsAvail / kModalItemH));
    h += m.items.empty() ? 56 : visibleItems * kModalItemH + 12;
    h = std::min(h, maxH);
    const int x = (kW - w) / 2, y = (kH - h) / 2;
    // tapping outside a dialog closes it
    addHit(0, 0, kW, kH, HitNone, 0, plat::BTN_B);
    addHit(x, y, w, h, HitNone, 0, 0);
    const uint32_t accent = m.accent ? m.accent : col::accent;
    c.roundRect(x, y, w, h, 18.f, hexColor(0x1F151B), withAlpha(accent, 200), 2.f);
    c.roundRect(x + 2, y + 2, w - 4, 6, 3.f, accent);
    c.text(font_, x + 32, y + 24, 25, col::text, m.title, w - 64);
    int ty = y + 72;
    for (int i = 0; i < static_cast<int>(lines.size()) && i < textLines; ++i) {
        if (ty > y + h - 40) break;
        c.text(font_, x + 32, ty, 18, col::dim, lines[static_cast<size_t>(i)], w - 64);
        ty += lh;
    }
    ty += 12;
    if (m.items.empty()) {
        const int bw = 30 + 8 + font_.measure("OK", 18);
        drawButtonBadge(c, x + w - 32 - bw, y + h - 46, "A");
        c.text(font_, x + w - 32 - bw + 38, y + h - 44, 18, col::text, "OK");
        addHit(x, y, w, h, HitNone, 0, plat::BTN_A);
        return;
    }
    m.list.clampTo(static_cast<int>(m.items.size()));
    m.list.ensureVisible(visibleItems);
    addArea(x, ty, w, visibleItems * kModalItemH, HitModal, kModalItemH, visibleItems, static_cast<int>(m.items.size()));
    for (int i = m.list.scroll; i < static_cast<int>(m.items.size()) && i < m.list.scroll + visibleItems; ++i) {
        const MenuItem& it = m.items[static_cast<size_t>(i)];
        const int iy = ty + (i - m.list.scroll) * kModalItemH;
        const bool sel = i == m.list.sel;
        if (sel) drawRowHighlight(c, x + 20, iy, w - 40, kModalItemH - 6);
        addHit(x + 20, iy, w - 40, kModalItemH - 6, HitModal, i);
        uint32_t lc = it.enabled ? (it.danger ? col::err : col::text) : col::muted;
        if (it.sub.empty()) {
            c.text(font_, x + 40, iy + (kModalItemH - 6 - font_.lineHeight(20)) / 2, 20, lc, it.label, w - 90);
        } else {
            c.text(font_, x + 40, iy + 6, 20, lc, it.label, w - 90);
            c.text(font_, x + 40, iy + 32, 15, it.enabled ? col::dim : col::muted, it.sub, w - 90);
        }
    }
    drawScrollbar(c, x + w - 16, ty, visibleItems * kModalItemH - 6, static_cast<int>(m.items.size()), visibleItems, m.list.scroll);
}

void App::drawHelp(Canvas& c) {
    addHit(0, 0, kW, kH, HitNone, 0, plat::BTN_B);
    const int x = 70, y = 40, w = kW - 140, h = kH - 80;
    c.roundRect(x, y, w, h, 18.f, hexColor(0x1F151B), withAlpha(col::accent, 200), 2.f);
    drawEmblem(c, x + 50.f, y + 46.f, 22.f);
    c.text(font_, x + 86, y + 24, 27, col::text, tr("Pomoc – Switch Mod Hub", "Help - Switch Mod Hub"));
    int ly = y + 86;
    const int colW = (w - 100) / 2;
    c.text(font_, x + 40, ly, 20, col::gold, tr("Jak zmienić zapis", "How to change a save"));
    int yy = ly + 34;
    const char* steps[] = {
        tr("Wybierz grę, potem „Zapis w grze” (prosto na konsoli) albo kopię JKSV. Mody: karta Mody (R).",
           "Pick a game, then \"Game save\" (right on the console) or a JKSV backup. Mods: the Mods tab (R)."),
        tr("Wybierz plik zapisu (w Silksongu userN.dat = slot N). Nieznane pliki otwierają się jako bajty (HEX).",
           "Pick a save file (in Silksong userN.dat = slot N). Unknown files open as bytes (hex)."),
        tr("Edytuj w kartach: Szybka edycja, Wszystkie dane, Akcje. Karta Zmiany pokazuje, co zmieniłeś.",
           "Edit on the tabs: Quick edit, All data, Actions. The Changes tab lists everything you changed."),
        tr("Naciśnij + i wybierz „Zapisz w grze” albo zapis jako kopia JKSV.",
           "Press + and pick \"Save to the game\" or save as a JKSV backup."),
        tr("Zapis w grze działa od razu. Kopię JKSV przywróć w JKSV (Restore).",
           "Saving to the game works right away. Restore a JKSV backup in JKSV (Restore)."),
    };
    for (int i = 0; i < 5; ++i) {
        c.circle(x + 54.f, yy + 11.f, 12.f, col::accentDim);
        c.text(font_, x + 42, yy, 15, col::text, std::to_string(i + 1), 24, Align::Center);
        yy += c.textWrapped(font_, x + 76, yy, colW - 50, 17, col::text, steps[i]) + 9;
    }
    yy += 4;
    c.textWrapped(font_, x + 40, yy, colW - 10, 15, col::muted,
                  tr("Gra musi być zamknięta, gdy zapisujesz w grze. Przed każdym zapisem edytor robi kopię bezpieczeństwa "
                     "(sdmc:/switch/SilksongSaveEditor/backups). Ekran dotykowy: stuknij, aby wybrać, stuknij ponownie, aby "
                     "otworzyć; przeciągnij, aby przewinąć. Log: sdmc:/switch/SilksongSaveEditor/log.txt",
                     "The game must be closed when you save to it. Before every save the editor makes a safety copy "
                     "(sdmc:/switch/SilksongSaveEditor/backups). Touch screen: tap to select, tap again to open, drag to "
                     "scroll. Log: sdmc:/switch/SilksongSaveEditor/log.txt"));

    const int cx = x + 60 + colW;
    c.text(font_, cx, ly, 20, col::gold, tr("Sterowanie", "Controls"));
    const std::pair<const char*, const char*> keys[] = {
        {"UD", tr("Wybór na liście (przytrzymaj = szybko)", "Move in the list (hold = fast)")},
        {"LR", tr("Zmień liczbę / przełącznik / stan", "Change a number / switch / state")},
        {"A", tr("Otwórz, edytuj, przełącz", "Open, edit, toggle")},
        {"B", tr("Wstecz / wyczyść filtr", "Back / clear the filter")},
        {"X", tr("Przywróć wartość, menu, ustawienia", "Restore a value, menu, settings")},
        {"Y", tr("Szukaj, następna sekcja, odśwież", "Search, next section, refresh")},
        {"L", tr("Poprzednia karta", "Previous tab")},
        {"R", tr("Następna karta", "Next tab")},
        {"ZL", tr("Strona w górę", "Page up")},
        {"ZR", tr("Strona w dół", "Page down")},
        {"+", tr("Zapisz (w edytorze) / wyjście", "Save (in the editor) / exit")},
        {"-", tr("Ta pomoc", "This help")},
    };
    yy = ly + 34;
    for (const auto& k : keys) {
        drawButtonBadge(c, cx, yy, k.first);
        c.text(font_, cx + 54, yy + 3, 17, col::text, k.second, colW - 60);
        yy += 35;
    }
    c.text(font_, x + w - 260, y + h - 36, 15, col::muted, tr("Naciśnij B, aby zamknąć", "Press B to close"));
}

void App::drawBusy(Canvas& c) {
    const int w = 560, h = 130;
    const int x = (kW - w) / 2, y = (kH - h) / 2;
    c.roundRect(x, y, w, h, 18.f, hexColor(0x1F151B), withAlpha(col::accent, 200), 2.f);
    c.text(font_, x + 30, y + 30, 22, col::text, busyMessage_, w - 60);
    const int phase = static_cast<int>((now_ / 180) % 4);
    for (int i = 0; i < 4; ++i)
        c.circle(x + 40.f + i * 26.f, y + 92.f, 7.f, i == phase ? col::accent : col::toggleOff);
    c.text(font_, x + 160, y + 82, 16, col::muted, tr("Nie wyłączaj konsoli...", "Do not turn off the console..."));
}
