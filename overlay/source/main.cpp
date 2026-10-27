// Switch Mod Hub overlay (Tesla Menu / Ultrahand): turn the mods of the running game on and off
// without leaving it. Uses the same mod engine as the app; the changes apply when the game restarts.
#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "i18n.h"
#include "modengine.h"
#include "settings.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

namespace {

bool g_ns = false;

mods::Paths modPaths() {
    mods::Paths p;
    p.sdRoot = "sdmc:/";
    p.modsDir = "sdmc:/switch/SwitchModHub/mods";
    return p;
}

u64 runningTitle() {
    u64 pid = 0, tid = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid)) || R_FAILED(pmdmntGetProgramId(&tid, pid))) return 0;
    return tid;
}

std::string titleName(u64 tid) {
    if (g_ns) {
        std::vector<u8> buf(sizeof(NsApplicationControlData));
        NsApplicationControlData* data = reinterpret_cast<NsApplicationControlData*>(buf.data());
        u64 size = 0;
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, data, buf.size(), &size))) {
            NacpLanguageEntry* e = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &e)) && e && e->name[0]) {
                const void* nul = std::memchr(e->name, 0, sizeof(e->name));
                const size_t len = nul ? static_cast<size_t>(static_cast<const char*>(nul) - e->name) : sizeof(e->name);
                return std::string(e->name, len);
            }
        }
    }
    return mods::titleHex(tid);
}

class GuiGame : public tsl::Gui {
public:
    explicit GuiGame(u64 tid) : tid_(tid) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Switch Mod Hub", titleName(tid_));
        auto* list = new tsl::elm::List();
        auto* status = new tsl::elm::ListItem(tr("Zmiany działają po restarcie gry", "Changes apply after restarting the game"));
        list->addItem(new tsl::elm::CategoryHeader(tr("Mody", "Mods")));
        const std::vector<mods::Mod> all = mods::list(modPaths(), tid_);
        if (all.empty()) list->addItem(new tsl::elm::ListItem(tr("Brak modów dla tej gry", "No mods for this game")));
        for (const mods::Mod& m : all) {
            auto* item = new tsl::elm::ToggleListItem(m.name, m.enabled, tr("Wł.", "On"), tr("Wył.", "Off"));
            const std::string id = m.id;
            const u64 tid = tid_;
            item->setStateChangedListener([item, status, id, tid](bool on) {
                std::string err;
                const bool ok = on ? mods::enable(modPaths(), tid, id, err) : mods::disable(modPaths(), tid, id, err);
                if (!ok) {
                    item->setState(!on);
                    status->setText(err);
                } else {
                    status->setText(on ? tr("Włączono – uruchom grę ponownie", "Enabled - restart the game")
                                       : tr("Wyłączono – uruchom grę ponownie", "Disabled - restart the game"));
                }
            });
            list->addItem(item);
        }
        list->addItem(new tsl::elm::CategoryHeader(tr("Informacja", "Info")));
        list->addItem(status);
        list->addItem(new tsl::elm::ListItem(tr("Cheaty włączysz w EdiZon", "Turn cheats on in EdiZon")));
        list->addItem(new tsl::elm::ListItem(tr("Nowe mody: aplikacja Switch Mod Hub", "New mods: the Switch Mod Hub app")));
        frame->setContent(list);
        return frame;
    }

private:
    u64 tid_;
};

class GuiMain : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Switch Mod Hub", "v" APP_VERSION_STR);
        auto* list = new tsl::elm::List();
        const u64 running = runningTitle();
        const std::vector<u64> games = mods::gamesWithMods(modPaths());
        auto addGame = [list](u64 tid) {
            const std::vector<mods::Mod> all = mods::list(modPaths(), tid);
            int on = 0;
            for (const mods::Mod& m : all) on += m.enabled ? 1 : 0;
            auto* item = new tsl::elm::ListItem(titleName(tid), std::to_string(on) + "/" + std::to_string(all.size()));
            item->setClickListener([tid](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<GuiGame>(tid);
                    return true;
                }
                return false;
            });
            list->addItem(item);
        };
        if (running) {
            list->addItem(new tsl::elm::CategoryHeader(tr("Uruchomiona gra", "Running game")));
            addGame(running);
        }
        list->addItem(new tsl::elm::CategoryHeader(tr("Gry z modami", "Games with mods")));
        int shown = 0;
        for (u64 tid : games) {
            if (tid == running) continue;
            addGame(tid);
            ++shown;
        }
        if (!shown && !running)
            list->addItem(new tsl::elm::ListItem(tr("Brak modów – pobierz je w aplikacji", "No mods - get them in the app")));
        frame->setContent(list);
        return frame;
    }
};

class ModHubOverlay : public tsl::Overlay {
public:
    void initServices() override {
        fsdevMountSdmc();
        g_ns = R_SUCCEEDED(nsInitialize());
        settings::load("sdmc:/switch/SwitchModHub/settings.json");
        i18n::setLanguage(settings::get().language);
    }
    void exitServices() override {
        if (g_ns) nsExit();
        fsdevUnmountDevice("sdmc");
    }
    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<GuiMain>(); }
};

} // namespace

int main(int argc, char** argv) { return tsl::loop<ModHubOverlay>(argc, argv); }
