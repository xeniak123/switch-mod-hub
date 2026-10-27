// App: mods of a game, the online mod repository and the self update.
#include <algorithm>
#include <cstdio>

#include "app.h"
#include "app_ui.h"
#include "crypto.h"
#include "fsutil.h"
#include "i18n.h"
#include "log.h"
#include "settings.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

using namespace ui;

namespace {

std::string kindsText(const mods::Mod& m) {
    std::string s;
    auto add = [&s](const char* k) { s += (s.empty() ? "" : ", ") + std::string(k); };
    if (m.romfs) add("romfs");
    if (m.exefs) add("exefs");
    if (m.cheats) add(tr("cheaty", "cheats"));
    if (m.patches) add(tr("łatki", "patches"));
    return s;
}

std::string joinTags(const std::vector<std::string>& tags) {
    std::string s;
    for (const std::string& t : tags) s += (s.empty() ? "" : ", ") + t;
    return s;
}

} // namespace

// =====================================================================================
// Mods of the open game
// =====================================================================================

void App::reloadGameMods() {
    gameMods_.clear();
    if (title_.titleId) gameMods_ = mods::list(modPaths_, title_.titleId);
    rebuildModRows();
}

const repo::RemoteMod* App::remoteFor(uint64_t titleId, const std::string& id) const {
    const jksv::TitleDir* t = titleById(titleId);
    const std::string name = t ? t->name : gameName(titleId);
    for (const repo::RemoteMod& r : repo_.mods)
        if (r.id == id && repo::matchesGame(r, titleId, name)) return &r;
    return nullptr;
}

void App::rebuildModRows() {
    modRows_.clear();
    for (size_t i = 0; i < gameMods_.size(); ++i) {
        ModRow r;
        r.local = static_cast<int>(i);
        for (size_t k = 0; k < repo_.mods.size(); ++k)
            if (repo_.mods[k].id == gameMods_[i].id && repo::matchesGame(repo_.mods[k], title_.titleId, title_.name))
                r.remote = static_cast<int>(k);
        modRows_.push_back(r);
    }
    if (title_.titleId) {
        for (size_t k = 0; k < repo_.mods.size(); ++k) {
            const repo::RemoteMod& m = repo_.mods[k];
            if (!repo::matchesGame(m, title_.titleId, title_.name)) continue;
            bool installed = false;
            for (const mods::Mod& l : gameMods_) installed = installed || l.id == m.id;
            if (installed) continue;
            ModRow r;
            r.remote = static_cast<int>(k);
            modRows_.push_back(r);
        }
    }
    modList_.clampTo(static_cast<int>(modRows_.size()));
}

void App::toggleMod(uint64_t titleId, const std::string& id, bool on) {
    runBusy(on ? tr("Włączanie moda...", "Enabling the mod...") : tr("Wyłączanie moda...", "Disabling the mod..."),
            [this, titleId, id, on] {
                std::string err;
                int skipped = 0;
                const bool ok = on ? mods::enable(modPaths_, titleId, id, err) : mods::disable(modPaths_, titleId, id, err, &skipped);
                logf("mod %s %s: %s", id.c_str(), on ? "enable" : "disable", ok ? "ok" : err.c_str());
                reloadGameMods();
                if (!ok) {
                    showMessage(on ? tr("Nie włączono moda", "Mod not enabled") : tr("Nie wyłączono moda", "Mod not disabled"), err, col::err);
                    return;
                }
                if (skipped > 0)
                    showMessage(tr("Mod wyłączony", "Mod disabled"),
                                i18n::plural(skipped, "plik został", "pliki zostały", "plików zostało", "file was", "files were") +
                                    tr(" zmieniony po włączeniu moda, więc zostawiono go na karcie SD.",
                                       " changed after the mod was enabled, so it was left on the SD card."),
                                col::warn);
                else if (plat::runningGame() == titleId)
                    toast(tr("Uruchom grę ponownie, aby zobaczyć zmianę", "Restart the game to see the change"), col::warn);
                else
                    toast(on ? tr("Mod włączony", "Mod enabled") : tr("Mod wyłączony", "Mod disabled"), col::ok);
            });
}

void App::installRemote(size_t remoteIndex, uint64_t titleId, bool enableAfter) {
    if (remoteIndex >= repo_.mods.size() || !titleId) return;
    const repo::RemoteMod m = repo_.mods[remoteIndex];
    std::string msg = tr("Pobieranie: ", "Downloading: ") + m.name;
    if (m.size) msg += " (" + formatSize(m.size) + ")";
    runBusy(msg + "...", [this, m, titleId, enableAfter] {
        std::vector<uint8_t> data;
        std::string err;
        if (!plat::networkAvailable()) {
            showMessage(tr("Brak internetu", "No internet"), tr("Połącz konsolę z internetem i spróbuj ponownie.",
                                                                "Connect the console to the internet and try again."), col::err);
            return;
        }
        if (!plat::httpGet(m.url, data, err)) {
            showMessage(tr("Nie pobrano moda", "Mod not downloaded"), err, col::err);
            return;
        }
        if ((m.size && data.size() != m.size) || crypto::sha256Hex(data.data(), data.size()) != m.sha256) {
            logf("mod %s: checksum mismatch (%u bytes)", m.id.c_str(), static_cast<unsigned>(data.size()));
            showMessage(tr("Nie zainstalowano moda", "Mod not installed"),
                        tr("Pobrany plik ma inną sumę kontrolną niż w repozytorium – został odrzucony.",
                           "The downloaded file has a different checksum than in the repository - it was rejected."),
                        col::err);
            return;
        }
        mods::Mod meta;
        meta.name = m.name;
        meta.version = m.version;
        meta.author = m.author;
        meta.description = m.description;
        meta.tags = m.tags;
        meta.source = "repo";
        if (!mods::installZip(modPaths_, titleId, m.id, data, meta, err)) {
            reloadGameMods();
            showMessage(tr("Nie zainstalowano moda", "Mod not installed"), err, col::err);
            return;
        }
        logf("installed mod %s %s for %s", m.id.c_str(), m.version.c_str(), mods::titleHex(titleId).c_str());
        if (enableAfter && !mods::isEnabled(modPaths_, titleId, m.id) && !mods::enable(modPaths_, titleId, m.id, err)) {
            reloadGameMods();
            loadTitles();
            showMessage(tr("Mod pobrany, ale nie włączony", "Mod downloaded but not enabled"), err, col::warn);
            return;
        }
        reloadGameMods();
        loadTitles();
        toast((enableAfter ? tr("Zainstalowano i włączono: ", "Installed and enabled: ") : tr("Pobrano: ", "Downloaded: ")) + m.name, col::ok);
    });
}

void App::removeLocalMod(uint64_t titleId, const std::string& id) {
    showConfirm(tr("Usunąć mod?", "Delete the mod?"),
                tr("Mod zostanie wyłączony i usunięty z biblioteki na karcie SD.", "The mod is disabled and deleted from the library on the SD card."),
                tr("Usuń", "Delete"), [this, titleId, id] {
                    std::string err;
                    if (!mods::removeMod(modPaths_, titleId, id, err))
                        showMessage(tr("Nie usunięto moda", "Mod not deleted"), err, col::err);
                    else
                        toast(tr("Usunięto mod", "Mod deleted"), col::ok);
                    reloadGameMods();
                    loadTitles();
                },
                true);
}

void App::modMenu(size_t row) {
    if (row >= modRows_.size()) return;
    const ModRow r = modRows_[row];
    const uint64_t tid = title_.titleId;
    Modal m;
    if (r.local >= 0) {
        const mods::Mod mod = gameMods_[static_cast<size_t>(r.local)];
        m.title = mod.name;
        MenuItem toggle;
        toggle.label = mod.enabled ? tr("Wyłącz", "Disable") : tr("Włącz", "Enable");
        toggle.sub = mod.enabled ? tr("Usuwa pliki moda z atmosphere/contents", "Removes the mod's files from atmosphere/contents")
                                 : tr("Kopiuje pliki moda do atmosphere/contents (LayeredFS)", "Copies the mod's files to atmosphere/contents (LayeredFS)");
        toggle.action = [this, tid, mod] { toggleMod(tid, mod.id, !mod.enabled); };
        m.items.push_back(toggle);
        if (r.remote >= 0 && repo::compareVersions(repo_.mods[static_cast<size_t>(r.remote)].version, mod.version) > 0) {
            MenuItem upd;
            upd.label = tr("Aktualizuj do wersji ", "Update to version ") + repo_.mods[static_cast<size_t>(r.remote)].version;
            upd.sub = tr("Pobiera nową wersję z repozytorium", "Downloads the new version from the repository");
            const size_t ri = static_cast<size_t>(r.remote);
            upd.action = [this, ri, tid] { installRemote(ri, tid, true); };
            m.items.push_back(upd);
        }
        MenuItem del;
        del.label = tr("Usuń z biblioteki", "Delete from the library");
        del.danger = true;
        del.action = [this, tid, mod] { removeLocalMod(tid, mod.id); };
        m.items.push_back(del);
    } else {
        const size_t ri = static_cast<size_t>(r.remote);
        m.title = repo_.mods[ri].name;
        MenuItem get;
        get.label = tr("Pobierz i włącz", "Download and enable");
        get.sub = repo_.mods[ri].size ? formatSize(repo_.mods[ri].size) : std::string();
        get.action = [this, ri, tid] { installRemote(ri, tid, true); };
        m.items.push_back(get);
        MenuItem only;
        only.label = tr("Tylko pobierz", "Download only");
        only.action = [this, ri, tid] { installRemote(ri, tid, false); };
        m.items.push_back(only);
    }
    MenuItem cancel;
    cancel.label = tr("Anuluj", "Cancel");
    m.items.push_back(cancel);
    modals_.push_back(m);
}

void App::importModMenu() {
    const std::string dir = plat::sdPath(kModImportDirSd);
    fsu::mkdirs(dir);
    std::vector<fsu::Entry> entries;
    fsu::listDir(dir, entries);
    std::sort(entries.begin(), entries.end(), [](const fsu::Entry& a, const fsu::Entry& b) { return a.name < b.name; });
    Modal m;
    m.title = tr("Importuj mod (.zip)", "Import a mod (.zip)");
    m.text = tr("Mod zostanie dodany do gry ", "The mod is added to ") + title_.name + ".";
    for (const fsu::Entry& e : entries) {
        if (e.isDir || e.name.size() < 5 || lowerAscii(e.name.substr(e.name.size() - 4)) != ".zip") continue;
        MenuItem it;
        it.label = e.name;
        it.sub = formatSize(e.size);
        const std::string path = fsu::join(dir, e.name);
        it.action = [this, path] { importModFile(path); };
        m.items.push_back(it);
    }
    if (m.items.empty()) {
        showMessage(tr("Brak modów do importu", "No mods to import"),
                    tr("Skopiuj archiwum moda (.zip) do folderu:\n", "Copy a mod archive (.zip) to the folder:\n") + std::string(kModImportDirSd) +
                        tr("\n\nArchiwum może zawierać romfs/, exefs/, cheats/ albo układ karty SD (atmosphere/contents/<ID gry>/...).",
                           "\n\nThe archive may contain romfs/, exefs/, cheats/ or an SD card layout (atmosphere/contents/<title id>/...)."));
        return;
    }
    MenuItem cancel;
    cancel.label = tr("Anuluj", "Cancel");
    m.items.push_back(cancel);
    modals_.push_back(m);
}

void App::importModFile(const std::string& path) {
    const uint64_t tid = title_.titleId;
    runBusy(tr("Importowanie moda...", "Importing the mod..."), [this, path, tid] {
        std::vector<uint8_t> data;
        std::string err;
        if (!fsu::readFile(path, data, err)) {
            showMessage(tr("Błąd odczytu", "Read error"), err, col::err);
            return;
        }
        std::string base = fsu::fileName(path);
        base = base.substr(0, base.size() - 4);
        mods::Mod meta;
        meta.name = base;
        meta.source = "local";
        const std::string id = mods::safeId(base);
        if (!mods::installZip(modPaths_, tid, id, data, meta, err)) {
            showMessage(tr("Nie zaimportowano moda", "Mod not imported"), err, col::err);
            return;
        }
        logf("imported mod %s for %s", id.c_str(), mods::titleHex(tid).c_str());
        reloadGameMods();
        loadTitles();
        for (size_t i = 0; i < modRows_.size(); ++i)
            if (modRows_[i].local >= 0 && gameMods_[static_cast<size_t>(modRows_[i].local)].id == id) modList_.sel = static_cast<int>(i);
        toast(tr("Zaimportowano – naciśnij A, aby włączyć", "Imported - press A to enable it"), col::ok);
    });
}

void App::updateGameMods(const plat::Input& in, const Nav& nav) {
    const int visible = (kBrowseH - kBrowseTabsH - 16) / kBrowseRowH;
    moveList(modList_, static_cast<int>(modRows_.size()), nav, in, visible);
    if ((in.down & plat::BTN_A) && !modRows_.empty()) {
        modMenu(static_cast<size_t>(modList_.sel));
    } else if (in.down & plat::BTN_Y) {
        reloadGameMods();
        ensureRepo(true, [this] { rebuildModRows(); });
    } else if (in.down & plat::BTN_B) {
        loadTitles();
        screen_ = Screen::Titles;
    } else if (in.down & plat::BTN_PLUS) {
        exit_ = true;
    }
}

void App::drawGameMods(Canvas& c) {
    const int listY = kBrowseY + kBrowseTabsH;
    const int listH = kBrowseH - kBrowseTabsH;
    if (!title_.titleId) {
        drawEmptyState(c, kListX, listY, kListW, listH, tr("Mody niedostępne", "Mods unavailable"),
                       tr("Ta gra nie jest zainstalowana na konsoli (nie ma numeru gry), więc nie można włączać jej modów.",
                          "This game is not installed on the console (no title id), so its mods cannot be enabled."));
    } else if (modRows_.empty()) {
        drawEmptyState(c, kListX, listY, kListW, listH, tr("Brak modów", "No mods"),
                       tr("Pobierz mody z karty Mody online albo skopiuj archiwum .zip do sdmc:/switch/SwitchModHub/import-mods "
                          "i wybierz X, potem Importuj mod.",
                          "Get mods from the Online mods tab, or copy a .zip archive to sdmc:/switch/SwitchModHub/import-mods "
                          "and pick X, then Import a mod."));
    } else {
        const int visible = (listH - 16) / kBrowseRowH;
        addArea(kListX, listY, kListW, listH, HitMods, kBrowseRowH, visible, static_cast<int>(modRows_.size()));
        for (int i = modList_.scroll; i < static_cast<int>(modRows_.size()) && i < modList_.scroll + visible; ++i) {
            const ModRow& r = modRows_[static_cast<size_t>(i)];
            const int y = listY + 8 + (i - modList_.scroll) * kBrowseRowH;
            if (i == modList_.sel) drawRowHighlight(c, kListX + 8, y, kListW - 28, kBrowseRowH - 6);
            addHit(kListX + 8, y, kListW - 28, kBrowseRowH - 6, HitMods, i);
            std::string name, sub, tag;
            uint32_t tagColor = col::dim, iconColor = col::dim;
            if (r.local >= 0) {
                const mods::Mod& m = gameMods_[static_cast<size_t>(r.local)];
                name = m.name;
                sub = (m.version.empty() ? std::string() : "v" + m.version + "  ·  ") + kindsText(m) + "  ·  " + formatSize(m.bytes);
                if (!m.author.empty()) sub += "  ·  " + m.author;
                tag = m.enabled ? tr("WŁĄCZONY", "ENABLED") : tr("WYŁĄCZONY", "DISABLED");
                tagColor = iconColor = m.enabled ? col::ok : col::dim;
                if (r.remote >= 0 && repo::compareVersions(repo_.mods[static_cast<size_t>(r.remote)].version, m.version) > 0) {
                    tag = tr("AKTUALIZACJA", "UPDATE");
                    tagColor = col::gold;
                }
            } else {
                const repo::RemoteMod& m = repo_.mods[static_cast<size_t>(r.remote)];
                name = m.name;
                sub = "v" + m.version + (m.author.empty() ? std::string() : "  ·  " + m.author) +
                      (m.size ? "  ·  " + formatSize(m.size) : std::string());
                tag = tr("DO POBRANIA", "AVAILABLE");
                tagColor = iconColor = col::joyBlue;
            }
            drawIconPuzzle(c, kListX + 26, y + 15, iconColor);
            c.text(font_, kListX + 74, y + 7, 21, col::text, name, kListW - 270);
            c.text(font_, kListX + 74, y + 36, 16, col::dim, sub, kListW - 270);
            drawTag(c, kListX + kListW - 180, y + 21, tag, tagColor);
        }
        drawScrollbar(c, kListX + kListW - 14, listY + 8, listH - 20, static_cast<int>(modRows_.size()), visible, modList_.scroll);
    }
    // details
    int y = kBrowseY + 22;
    const int ix = kInfoX + 22, iw = kInfoW - 44;
    if (!modRows_.empty()) {
        const ModRow& r = modRows_[static_cast<size_t>(modList_.sel)];
        std::string name, desc, line1, line2, tags;
        if (r.local >= 0) {
            const mods::Mod& m = gameMods_[static_cast<size_t>(r.local)];
            name = m.name;
            desc = m.description;
            line1 = (m.version.empty() ? std::string() : tr("Wersja ", "Version ") + m.version + "  ·  ") +
                    (m.author.empty() ? std::string(m.source == "repo" ? tr("z repozytorium", "from the repository") : tr("mod lokalny", "local mod"))
                                      : m.author);
            line2 = i18n::plural(m.files, "plik", "pliki", "plików", "file", "files") + "  ·  " + formatSize(m.bytes) + "  ·  " + kindsText(m);
            tags = joinTags(m.tags);
        } else {
            const repo::RemoteMod& m = repo_.mods[static_cast<size_t>(r.remote)];
            name = m.name;
            desc = m.description;
            line1 = tr("Wersja ", "Version ") + m.version + (m.author.empty() ? std::string() : "  ·  " + m.author);
            line2 = (m.size ? formatSize(m.size) + "  ·  " : std::string()) + m.type +
                    (m.gameVersion.empty() ? std::string() : tr("  ·  gra ", "  ·  game ") + m.gameVersion);
            tags = joinTags(m.tags);
        }
        y += c.textWrapped(font_, ix, y, iw, 23, col::text, name, 2) + 6;
        c.text(font_, ix, y, 16, col::gold, line1, iw);
        y += 26;
        c.text(font_, ix, y, 15, col::dim, line2, iw);
        y += 30;
        if (!desc.empty()) y += c.textWrapped(font_, ix, y, iw, 17, col::text, desc, 7) + 10;
        if (!tags.empty()) y += c.textWrapped(font_, ix, y, iw, 15, col::muted, tr("Tagi: ", "Tags: ") + tags, 2) + 10;
    } else {
        c.text(font_, ix, y, 24, col::gold, tr("Mody", "Mods"));
        y += 46;
    }
    std::string note = tr("Mody działają przez LayeredFS w Atmosphère (atmosphere/contents/", "Mods work through LayeredFS in Atmosphère (atmosphere/contents/") +
                       mods::titleHex(title_.titleId) + tr("). Zmiany widać po ponownym uruchomieniu gry. Cheaty włączysz w EdiZon.",
                                                           "). Changes show after restarting the game. Turn cheats on in EdiZon.");
    if (!repoError_.empty() && repo_.mods.empty()) note += std::string("\n") + tr("Repozytorium: ", "Repository: ") + repoError_;
    c.textWrapped(font_, ix, kBrowseY + kBrowseH - 120, iw, 15, col::muted, note, 6);
    drawFooter(c, {{"A", tr("Wybierz", "Select")}, {"B", tr("Wstecz", "Back")}, {"X", "Menu"}, {"Y", tr("Odśwież", "Refresh")}, {"-", tr("Pomoc", "Help")}},
               tr("Mody gry", "Game mods"));
}

// =====================================================================================
// Online repository
// =====================================================================================

void App::ensureRepo(bool refresh, std::function<void()> then) {
    if (repoTried_ && !refresh) {
        if (then) then();
        return;
    }
    runBusy(tr("Pobieranie listy modów...", "Downloading the mod list..."), [this, refresh, then] {
        loadRepoNow(refresh);
        if (then) then();
    });
}

void App::loadRepoNow(bool) {
    repoTried_ = true;
    repoError_.clear();
    repoFromCache_ = false;
    const std::string cache = fsu::join(plat::sdPath(kCacheDirSd), "index.json");
    std::vector<uint8_t> data;
    std::string err;
    bool ok = false;
    if (!plat::networkAvailable()) {
        err = tr("Brak połączenia z internetem.", "No internet connection.");
    } else if (plat::httpGet(repo::kIndexUrl, data, err)) {
        repo::Index idx;
        if (repo::parseIndex(data, idx, err)) {
            repo_ = idx;
            ok = true;
            std::string e2;
            fsu::mkdirs(plat::sdPath(kCacheDirSd));
            fsu::replaceFile(cache, data.data(), data.size(), e2);
        }
    }
    if (!ok) {
        repoError_ = err;
        std::vector<uint8_t> cached;
        std::string e2;
        repo::Index idx;
        if (fsu::readFile(cache, cached, e2) && repo::parseIndex(cached, idx, e2)) {
            repo_ = idx;
            repoFromCache_ = true;
        }
    }
    logf("mod index: %u mods (%s)%s", static_cast<unsigned>(repo_.mods.size()), ok ? "online" : repoFromCache_ ? "cache" : "none",
         ok ? "" : (" - " + err).c_str());
    rebuildRepoRows();
    rebuildModRows();
}

uint64_t App::titleForRemote(const repo::RemoteMod& m) const {
    for (const jksv::TitleDir& t : titles_)
        if (t.titleId && repo::matchesGame(m, t.titleId, t.name)) return t.titleId;
    return 0;
}

void App::rebuildRepoRows() {
    repoRows_.clear();
    for (size_t i = 0; i < repo_.mods.size(); ++i) {
        const repo::RemoteMod& m = repo_.mods[i];
        if (!repoAllGames_ && !titleForRemote(m)) continue;
        if (!repo::matchesQuery(m, repoQuery_)) continue;
        repoRows_.push_back(i);
    }
    repoList_.clampTo(static_cast<int>(repoRows_.size()));
}

void App::repoMenu(size_t row) {
    if (row >= repoRows_.size()) return;
    const size_t ri = repoRows_[row];
    const repo::RemoteMod& m = repo_.mods[ri];
    const uint64_t tid = titleForRemote(m);
    Modal md;
    md.title = m.name;
    md.text = m.game + "  ·  v" + m.version;
    bool installed = false, enabled = false;
    std::string localVersion;
    if (tid) {
        for (const mods::Mod& l : mods::list(modPaths_, tid)) {
            if (l.id != m.id) continue;
            installed = true;
            enabled = l.enabled;
            localVersion = l.version;
        }
    }
    if (!tid) {
        MenuItem none;
        none.label = tr("Pobierz i włącz", "Download and enable");
        none.sub = tr("Ta gra nie jest zainstalowana na konsoli", "This game is not installed on the console");
        none.enabled = false;
        md.items.push_back(none);
    } else if (!installed) {
        MenuItem get;
        get.label = tr("Pobierz i włącz", "Download and enable");
        get.sub = m.size ? formatSize(m.size) : std::string();
        get.action = [this, ri, tid] { installRemote(ri, tid, true); };
        md.items.push_back(get);
        MenuItem only;
        only.label = tr("Tylko pobierz", "Download only");
        only.action = [this, ri, tid] { installRemote(ri, tid, false); };
        md.items.push_back(only);
    } else {
        if (repo::compareVersions(m.version, localVersion) > 0) {
            MenuItem upd;
            upd.label = tr("Aktualizuj do wersji ", "Update to version ") + m.version;
            upd.sub = tr("Zainstalowana: ", "Installed: ") + localVersion;
            upd.action = [this, ri, tid] { installRemote(ri, tid, true); };
            md.items.push_back(upd);
        }
        MenuItem toggle;
        toggle.label = enabled ? tr("Wyłącz", "Disable") : tr("Włącz", "Enable");
        const std::string id = m.id;
        toggle.action = [this, tid, id, enabled] { toggleMod(tid, id, !enabled); };
        md.items.push_back(toggle);
    }
    if (tid) {
        MenuItem open;
        open.label = tr("Otwórz grę (karta Mody)", "Open the game (Mods tab)");
        open.action = [this, tid] {
            const jksv::TitleDir* t = titleById(tid);
            if (!t) return;
            openTitle(*t);
            gameTab_ = 1;
            rebuildModRows();
        };
        md.items.push_back(open);
    }
    MenuItem cancel;
    cancel.label = tr("Anuluj", "Cancel");
    md.items.push_back(cancel);
    modals_.push_back(md);
}

void App::updateRepo(const plat::Input& in, const Nav& nav) {
    const int visible = (kBrowseH - kBrowseTabsH - 16) / kBrowseRowH;
    moveList(repoList_, static_cast<int>(repoRows_.size()), nav, in, visible);
    if ((in.down & plat::BTN_A) && !repoRows_.empty()) {
        repoMenu(static_cast<size_t>(repoList_.sel));
    } else if (in.down & plat::BTN_Y) {
        if (!repoTried_) {
            ensureRepo(false, [this] { rebuildRepoRows(); });
            return;
        }
        plat::TextRequest req;
        req.header = tr("Szukaj modów", "Search mods");
        req.guide = tr("Nazwa, gra, autor albo tag (puste = wszystkie)", "Name, game, author or tag (empty = all)");
        req.initial = repoQuery_;
        req.maxLength = 64;
        std::string out;
        if (plat::textInput(req, out)) {
            repoQuery_ = out;
            repoList_ = ListState();
            rebuildRepoRows();
        }
    } else if (in.down & plat::BTN_B) {
        if (!repoQuery_.empty()) {
            repoQuery_.clear();
            repoList_ = ListState();
            rebuildRepoRows();
        } else {
            homeTab_ = 0;
        }
    } else if (in.down & plat::BTN_PLUS) {
        exit_ = true;
    }
}

void App::drawRepo(Canvas& c) {
    const int listY = kBrowseY + kBrowseTabsH;
    const int listH = kBrowseH - kBrowseTabsH;
    if (!repoQuery_.empty()) {
        const std::string chip = tr("Szukaj: ", "Search: ") + repoQuery_ + "  (" + std::to_string(repoRows_.size()) + ")";
        const int w = std::min(300, font_.measure(chip, 14) + 20);
        c.roundRect(kListX + kListW - w - 22, kBrowseY + 13, w, 26, 8.f, withAlpha(col::accent, 50), col::accent, 1.f);
        c.text(font_, kListX + kListW - w - 22, kBrowseY + 13 + (26 - font_.lineHeight(14)) / 2, 14, col::text, chip, w, Align::Center);
    }
    if (repoRows_.empty()) {
        std::string title, text;
        if (!repoTried_) {
            title = tr("Lista modów nie jest pobrana", "The mod list is not downloaded");
            text = tr("Naciśnij Y, aby pobrać listę modów z GitHuba.", "Press Y to download the mod list from GitHub.");
        } else if (repo_.mods.empty()) {
            title = tr("Nie można pobrać listy modów", "Cannot download the mod list");
            text = repoError_.empty() ? std::string(tr("Repozytorium nie zawiera jeszcze modów.", "The repository has no mods yet.")) : repoError_;
        } else if (!repoQuery_.empty()) {
            title = tr("Brak wyników", "No results");
            text = tr("Żaden mod nie pasuje do wyszukiwania. Naciśnij B, aby je wyczyścić.", "No mod matches the search. Press B to clear it.");
        } else {
            title = tr("Brak modów do Twoich gier", "No mods for your games");
            text = tr("W repozytorium nie ma modów do zainstalowanych gier. X, potem „Pokaż mody do wszystkich gier”.",
                      "The repository has no mods for the installed games. X, then \"Show mods for all games\".");
        }
        drawEmptyState(c, kListX, listY, kListW, listH, title, text);
    } else {
        const int visible = (listH - 16) / kBrowseRowH;
        addArea(kListX, listY, kListW, listH, HitRepo, kBrowseRowH, visible, static_cast<int>(repoRows_.size()));
        for (int i = repoList_.scroll; i < static_cast<int>(repoRows_.size()) && i < repoList_.scroll + visible; ++i) {
            const repo::RemoteMod& m = repo_.mods[repoRows_[static_cast<size_t>(i)]];
            const int y = listY + 8 + (i - repoList_.scroll) * kBrowseRowH;
            if (i == repoList_.sel) drawRowHighlight(c, kListX + 8, y, kListW - 28, kBrowseRowH - 6);
            addHit(kListX + 8, y, kListW - 28, kBrowseRowH - 6, HitRepo, i);
            const uint64_t tid = titleForRemote(m);
            std::string tag;
            uint32_t tagColor = col::joyBlue;
            if (tid) {
                for (const jksv::TitleDir& t : titles_) {
                    if (t.titleId != tid) continue;
                    for (const mods::Mod& l : mods::list(modPaths_, tid)) {
                        if (l.id != m.id) continue;
                        tag = l.enabled ? tr("WŁĄCZONY", "ENABLED") : tr("POBRANY", "DOWNLOADED");
                        tagColor = l.enabled ? col::ok : col::dim;
                        if (repo::compareVersions(m.version, l.version) > 0) {
                            tag = tr("AKTUALIZACJA", "UPDATE");
                            tagColor = col::gold;
                        }
                    }
                    break;
                }
            } else {
                tag = tr("BRAK GRY", "NO GAME");
                tagColor = col::muted;
            }
            drawIconPuzzle(c, kListX + 26, y + 15, tid ? col::joyBlue : col::muted);
            c.text(font_, kListX + 74, y + 7, 21, col::text, m.name, kListW - 280);
            c.text(font_, kListX + 74, y + 36, 16, col::dim, m.game + "  ·  v" + m.version + (m.author.empty() ? std::string() : "  ·  " + m.author),
                   kListW - 280);
            if (!tag.empty()) drawTag(c, kListX + kListW - 180, y + 21, tag, tagColor);
        }
        drawScrollbar(c, kListX + kListW - 14, listY + 8, listH - 20, static_cast<int>(repoRows_.size()), visible, repoList_.scroll);
    }
    int y = kBrowseY + 22;
    const int ix = kInfoX + 22, iw = kInfoW - 44;
    if (!repoRows_.empty()) {
        const repo::RemoteMod& m = repo_.mods[repoRows_[static_cast<size_t>(repoList_.sel)]];
        y += c.textWrapped(font_, ix, y, iw, 23, col::text, m.name, 2) + 6;
        c.text(font_, ix, y, 16, col::gold, m.game, iw);
        y += 26;
        c.text(font_, ix, y, 15, col::dim,
               tr("Wersja ", "Version ") + m.version + (m.author.empty() ? std::string() : "  ·  " + m.author) +
                   (m.size ? "  ·  " + formatSize(m.size) : std::string()),
               iw);
        y += 30;
        if (!m.description.empty()) y += c.textWrapped(font_, ix, y, iw, 17, col::text, m.description, 8) + 10;
        if (!m.tags.empty()) y += c.textWrapped(font_, ix, y, iw, 15, col::muted, tr("Tagi: ", "Tags: ") + joinTags(m.tags), 2) + 10;
    } else {
        c.text(font_, ix, y, 24, col::gold, tr("Mody online", "Online mods"));
    }
    std::string note = std::string("github.com/") + repo::kRepo;
    if (repoFromCache_) note += tr("\nBez internetu – lista z pamięci podręcznej.", "\nOffline - the list comes from the cache.");
    if (!repo_.updated.empty()) note += tr("\nLista z dnia ", "\nList from ") + repo_.updated;
    if (repo_.skipped > 0) note += "\n" + i18n::plural(repo_.skipped, "pominięty wpis", "pominięte wpisy", "pominiętych wpisów", "skipped entry", "skipped entries");
    c.textWrapped(font_, ix, kBrowseY + kBrowseH - 96, iw, 15, col::muted, note, 5);
    drawFooter(c, {{"A", tr("Wybierz", "Select")}, {"Y", tr("Szukaj", "Search")}, {"X", "Menu"}, {"B", tr("Wstecz", "Back")}, {"-", tr("Pomoc", "Help")}},
               tr("Mody online", "Online mods"));
}

// =====================================================================================
// Self update
// =====================================================================================

void App::checkForUpdate(bool interactive) {
    if (!plat::networkAvailable()) {
        if (interactive)
            showMessage(tr("Brak internetu", "No internet"), tr("Połącz konsolę z internetem, aby sprawdzić aktualizacje.",
                                                                "Connect the console to the internet to check for updates."));
        return;
    }
    std::vector<uint8_t> data;
    std::string err;
    update::Release rel;
    if (!plat::httpGet(update::kLatestUrl, data, err) || !update::parseLatest(data, rel, err)) {
        logf("update check: %s", err.c_str());
        if (interactive) showMessage(tr("Nie sprawdzono aktualizacji", "Update check failed"), err, col::err);
        return;
    }
    logf("update check: latest %s, running %s", rel.tag.c_str(), APP_VERSION_STR);
    if (!update::isNewer(rel.tag, APP_VERSION_STR)) {
        if (interactive)
            showMessage(tr("Masz najnowszą wersję", "You have the latest version"),
                        std::string("Switch Mod Hub ") + APP_VERSION_STR + tr(" jest aktualny.", " is up to date."), col::ok);
        return;
    }
    std::string notes = rel.notes;
    if (notes.size() > 600) notes = notes.substr(0, 600) + "...";
    showConfirm(tr("Dostępna aktualizacja ", "Update available ") + rel.tag,
                tr("Zainstalowana wersja: ", "Installed version: ") + std::string(APP_VERSION_STR) + (notes.empty() ? std::string() : "\n\n" + notes),
                tr("Zaktualizuj teraz", "Update now"), [this, rel] {
                    runBusy(tr("Pobieranie aktualizacji...", "Downloading the update..."), [this, rel] { downloadAndInstallUpdate(rel); });
                });
}

void App::downloadAndInstallUpdate(const update::Release& rel) {
    std::vector<uint8_t> data;
    std::string err;
    if (!plat::httpGet(rel.url, data, err) || !update::install(plat::selfPath(), data, rel, err)) {
        logf("update failed: %s", err.c_str());
        showMessage(tr("Aktualizacja nie powiodła się", "The update failed"), err + tr("\n\nObecna wersja działa dalej bez zmian.", "\n\nThe current version keeps working unchanged."), col::err);
        return;
    }
    logf("updated to %s (%s)", rel.tag.c_str(), plat::selfPath().c_str());
    const bool restart = plat::setNextLoad(plat::selfPath());
    Modal m;
    m.title = tr("Zaktualizowano do ", "Updated to ") + rel.tag;
    m.text = restart ? tr("Aplikacja uruchomi się ponownie w nowej wersji.", "The app restarts in the new version.")
                     : tr("Zamknij aplikację i uruchom ją ponownie z Homebrew Menu.", "Close the app and start it again from the Homebrew Menu.");
    m.accent = col::ok;
    MenuItem go;
    go.label = restart ? tr("Uruchom ponownie", "Restart") : tr("Zamknij aplikację", "Close the app");
    go.action = [this] { exit_ = true; };
    m.items.push_back(go);
    modals_.push_back(m);
}
