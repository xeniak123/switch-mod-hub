// libnx implementation of the platform layer.
#ifdef __SWITCH__

#include <switch.h>

#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "i18n.h"
#include "log.h"
#include "platform.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

namespace {

Framebuffer g_fb;
bool g_fbReady = false;
PadState g_pad;
bool g_plReady = false;
std::vector<plat::FontBlob> g_fonts;
std::string g_selfPath = "sdmc:/switch/SwitchModHub/SwitchModHub.nro";

// ---- save data ----
const char* const kSaveDevice = "ssesave";

struct SaveRec {
    u64 appId;
    AccountUid uid;
    u64 saveId;
    u8 spaceId;
};

bool g_accReady = false;
bool g_nsReady = false;
bool g_pmReady = false;
bool g_nifmReady = false;
bool g_socketReady = false;
bool g_curlReady = false;
bool g_scanned = false;
std::string g_scanError;
std::vector<SaveRec> g_saves;
bool g_mounted = false;

uint32_t mapButtons(u64 b) {
    uint32_t r = 0;
    if (b & HidNpadButton_A) r |= plat::BTN_A;
    if (b & HidNpadButton_B) r |= plat::BTN_B;
    if (b & HidNpadButton_X) r |= plat::BTN_X;
    if (b & HidNpadButton_Y) r |= plat::BTN_Y;
    if (b & HidNpadButton_L) r |= plat::BTN_L;
    if (b & HidNpadButton_R) r |= plat::BTN_R;
    if (b & HidNpadButton_ZL) r |= plat::BTN_ZL;
    if (b & HidNpadButton_ZR) r |= plat::BTN_ZR;
    if (b & HidNpadButton_Plus) r |= plat::BTN_PLUS;
    if (b & HidNpadButton_Minus) r |= plat::BTN_MINUS;
    if (b & HidNpadButton_AnyUp) r |= plat::BTN_UP;
    if (b & HidNpadButton_AnyDown) r |= plat::BTN_DOWN;
    if (b & HidNpadButton_AnyLeft) r |= plat::BTN_LEFT;
    if (b & HidNpadButton_AnyRight) r |= plat::BTN_RIGHT;
    return r;
}

std::string resultCode(Result rc) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%04u", 2000u + R_MODULE(rc), R_DESCRIPTION(rc));
    return buf;
}

std::string uidHex(const AccountUid& u) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%016llX%016llX", static_cast<unsigned long long>(u.uid[1]),
                  static_cast<unsigned long long>(u.uid[0]));
    return buf;
}

std::string titleHex(u64 tid) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(tid));
    return buf;
}

bool initAccounts() {
    if (g_accReady) return true;
    const AccountServiceType types[] = {AccountServiceType_Administrator, AccountServiceType_System,
                                        AccountServiceType_Application};
    for (AccountServiceType t : types) {
        if (R_SUCCEEDED(accountInitialize(t))) {
            g_accReady = true;
            break;
        }
    }
    return g_accReady;
}

bool initNs() {
    if (!g_nsReady) g_nsReady = R_SUCCEEDED(nsInitialize());
    return g_nsReady;
}

bool scanSaves(std::string& err) {
    if (g_scanned) {
        err = g_scanError;
        return g_scanError.empty();
    }
    g_scanned = true;
    FsSaveDataInfoReader reader;
    const Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        g_scanError = std::string(tr("Nie można odczytać listy zapisów konsoli (kod ", "Cannot list the console's save data (code ")) +
                      resultCode(rc) + ").";
        err = g_scanError;
        return false;
    }
    FsSaveDataInfo chunk[32];
    for (;;) {
        s64 n = 0;
        if (R_FAILED(fsSaveDataInfoReaderRead(&reader, chunk, 32, &n)) || n <= 0) break;
        for (s64 i = 0; i < n; ++i)
            if (chunk[i].save_data_type == FsSaveDataType_Account && chunk[i].application_id)
                g_saves.push_back({chunk[i].application_id, chunk[i].uid, chunk[i].save_data_id, chunk[i].save_data_space_id});
    }
    fsSaveDataInfoReaderClose(&reader);
    logf("console saves: %u", static_cast<unsigned>(g_saves.size()));
    return true;
}

const SaveRec* findSave(u64 tid, const std::string& userId) {
    for (const SaveRec& s : g_saves)
        if (s.appId == tid && uidHex(s.uid) == userId) return &s;
    return nullptr;
}

bool initNetwork(std::string& err) {
    if (!g_socketReady) {
        const Result rc = socketInitializeDefault();
        if (R_FAILED(rc)) {
            err = std::string(tr("Nie można uruchomić sieci (kod ", "Cannot start networking (code ")) + resultCode(rc) + ").";
            return false;
        }
        g_socketReady = true;
    }
    if (!g_curlReady) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            err = tr("Nie można uruchomić biblioteki sieciowej.", "Cannot start the network library.");
            return false;
        }
        g_curlReady = true;
    }
    return true;
}

struct HttpContext {
    std::vector<uint8_t>* out;
    const std::function<bool(uint64_t, uint64_t)>* progress;
};

size_t httpWrite(char* data, size_t size, size_t count, void* user) {
    HttpContext* ctx = static_cast<HttpContext*>(user);
    const size_t n = size * count;
    if (ctx->out->size() + n > 128u * 1024u * 1024u) return 0;  // refuse absurdly large downloads
    ctx->out->insert(ctx->out->end(), reinterpret_cast<uint8_t*>(data), reinterpret_cast<uint8_t*>(data) + n);
    return n;
}

int httpProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    HttpContext* ctx = static_cast<HttpContext*>(user);
    if (ctx->progress && *ctx->progress &&
        !(*ctx->progress)(static_cast<uint64_t>(now < 0 ? 0 : now), static_cast<uint64_t>(total < 0 ? 0 : total)))
        return 1;
    return 0;
}

} // namespace

namespace plat {

void setArgs(int argc, char** argv) {
    if (argc > 0 && argv && argv[0] && argv[0][0]) {
        std::string p = argv[0];
        if (p[0] == '/') p = "sdmc:" + p;
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".nro") == 0) g_selfPath = p;
    }
}

bool init() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    hidInitializeTouchScreen();

    if (R_FAILED(framebufferCreate(&g_fb, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_RGBA_8888, 2))) return false;
    if (R_FAILED(framebufferMakeLinear(&g_fb))) {
        framebufferClose(&g_fb);
        return false;
    }
    g_fbReady = true;

    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        g_plReady = true;
        const PlSharedFontType order[] = {PlSharedFontType_Standard,           PlSharedFontType_ChineseSimplified,
                                          PlSharedFontType_ExtChineseSimplified, PlSharedFontType_ChineseTraditional,
                                          PlSharedFontType_KO,                 PlSharedFontType_NintendoExt};
        for (PlSharedFontType t : order) {
            PlFontData fd;
            if (R_SUCCEEDED(plGetSharedFontByType(&fd, t)) && fd.address && fd.size) {
                FontBlob b;
                b.data = static_cast<const uint8_t*>(fd.address);
                b.size = fd.size;
                g_fonts.push_back(b);
            }
        }
    }
    return true;
}

void shutdown() {
    liveUnmount();
    if (g_curlReady) curl_global_cleanup();
    if (g_socketReady) socketExit();
    if (g_accReady) accountExit();
    if (g_nsReady) nsExit();
    if (g_pmReady) pmdmntExit();
    if (g_nifmReady) nifmExit();
    g_curlReady = g_socketReady = g_accReady = g_nsReady = g_pmReady = g_nifmReady = false;
    if (g_fbReady) framebufferClose(&g_fb);
    g_fbReady = false;
    if (g_plReady) plExit();
    g_plReady = false;
    g_fonts.clear();
}

bool running() { return appletMainLoop(); }

Input pollInput() {
    padUpdate(&g_pad);
    Input in;
    in.down = mapButtons(padGetButtonsDown(&g_pad));
    in.held = mapButtons(padGetButtons(&g_pad));
    HidTouchScreenState ts;
    std::memset(&ts, 0, sizeof(ts));
    if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
        in.touching = true;
        in.touchX = static_cast<int>(ts.touches[0].x);
        in.touchY = static_cast<int>(ts.touches[0].y);
    }
    return in;
}

uint32_t* beginFrame(int& width, int& height, int& stridePixels) {
    u32 stride = 0;
    void* px = framebufferBegin(&g_fb, &stride);
    width = 1280;
    height = 720;
    stridePixels = static_cast<int>(stride / sizeof(uint32_t));
    return static_cast<uint32_t*>(px);
}

void endFrame() { framebufferEnd(&g_fb); }

uint64_t nowMs() { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }

bool textInput(const TextRequest& req, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    if (req.numeric) {
        swkbdConfigSetType(&kbd, SwkbdType_NumPad);
        if (req.allowNegative) swkbdConfigSetLeftOptionalSymbolKey(&kbd, "-");
        if (req.allowDecimal) swkbdConfigSetRightOptionalSymbolKey(&kbd, ".");
    }
    swkbdConfigSetHeaderText(&kbd, req.header.c_str());
    swkbdConfigSetSubText(&kbd, "");
    swkbdConfigSetGuideText(&kbd, req.guide.c_str());
    swkbdConfigSetInitialText(&kbd, req.initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(req.maxLength > 0 ? req.maxLength : 256));
    std::vector<char> buf(static_cast<size_t>(req.maxLength > 0 ? req.maxLength : 256) * 4 + 16, '\0');
    const Result rc = swkbdShow(&kbd, buf.data(), buf.size());
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = buf.data();
    return true;
}

std::vector<FontBlob> fonts() { return g_fonts; }

std::string sdPath(const std::string& sdmcPath) { return sdmcPath; }

std::string localTimeStamp() {
    char buf[64];
    u64 ts = 0;
    TimeCalendarTime ct;
    TimeCalendarAdditionalInfo info;
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &ts)) &&
        R_SUCCEEDED(timeToCalendarTimeWithMyRule(ts, &ct, &info))) {
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u @ %02u.%02u.%02u", ct.year, ct.month, ct.day, ct.hour,
                      ct.minute, ct.second);
        return buf;
    }
    const time_t t = time(nullptr);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d @ %02d.%02d.%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
                  tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

// SD card timestamps can be reported in an unknown time zone, so they are not shown on the console
// (the JKSV backup name already contains the date). They are still used for sorting.
std::string formatTime(int64_t) { return ""; }

bool isAppletMode() {
    const AppletType t = appletGetAppletType();
    return t != AppletType_Application && t != AppletType_SystemApplication;
}

bool listGames(std::vector<GameInfo>& out, std::string& err) {
    out.clear();
    if (!initNs()) {
        err = tr("Nie można odczytać listy gier.", "Cannot read the list of games.");
        return false;
    }
    std::vector<NsApplicationRecord> records(64);
    std::vector<uint8_t> buf(sizeof(NsApplicationControlData));
    NsApplicationControlData* data = reinterpret_cast<NsApplicationControlData*>(buf.data());
    s32 offset = 0;
    for (;;) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records.data(), static_cast<s32>(records.size()), offset, &count)) || count <= 0)
            break;
        for (s32 i = 0; i < count; ++i) {
            GameInfo g;
            g.titleId = records[static_cast<size_t>(i)].application_id;
            u64 size = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, g.titleId, data, buf.size(), &size)) &&
                size >= sizeof(data->nacp)) {
                NacpLanguageEntry* e = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &e)) && e) g.name.assign(e->name, strnlen(e->name, sizeof(e->name)));
                const u64 iconSize = size - sizeof(data->nacp);
                if (iconSize > 0 && iconSize <= sizeof(data->icon)) g.iconJpeg.assign(data->icon, data->icon + iconSize);
            }
            if (g.name.empty()) g.name = titleHex(g.titleId);
            out.push_back(std::move(g));
        }
        offset += count;
        if (count < static_cast<s32>(records.size())) break;
    }
    logf("installed games: %u", static_cast<unsigned>(out.size()));
    return true;
}

std::vector<uint64_t> titlesWithSaves() {
    std::string err;
    std::vector<uint64_t> out;
    scanSaves(err);
    for (const SaveRec& s : g_saves)
        if (std::find(out.begin(), out.end(), s.appId) == out.end()) out.push_back(s.appId);
    return out;
}

uint64_t runningGame() {
    if (!g_pmReady) g_pmReady = R_SUCCEEDED(pmdmntInitialize());
    if (!g_pmReady) return 0;
    u64 pid = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid))) return 0;  // no game running
    u64 self = 0;
    if (R_SUCCEEDED(svcGetProcessId(&self, CUR_PROCESS_HANDLE)) && self == pid) return 0;  // title takeover
    u64 program = 0;
    if (R_FAILED(pmdmntGetProgramId(&program, pid))) return 0;
    return program;
}

bool liveUsers(uint64_t titleId, std::vector<SaveUser>& out, std::string& err) {
    out.clear();
    if (!scanSaves(err)) return false;
    initAccounts();
    for (const SaveRec& s : g_saves) {
        if (s.appId != titleId) continue;
        SaveUser u;
        u.id = uidHex(s.uid);
        AccountProfile prof;
        if (g_accReady && R_SUCCEEDED(accountGetProfile(&prof, s.uid))) {
            AccountProfileBase base;
            std::memset(&base, 0, sizeof(base));
            if (R_SUCCEEDED(accountProfileGet(&prof, nullptr, &base)))
                u.name.assign(base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
            accountProfileClose(&prof);
        }
        if (u.name.empty()) u.name = tr("Użytkownik ", "User ") + std::to_string(out.size() + 1);
        out.push_back(u);
    }
    return true;
}

bool liveMount(uint64_t titleId, const std::string& userId, bool readOnly, std::string& root, std::string& err) {
    liveUnmount();
    const SaveRec* s = findSave(titleId, userId);
    if (!s) {
        err = tr("Nie znaleziono zapisu gry tego użytkownika.", "No game save found for this user.");
        return false;
    }
    const Result rc = readOnly ? fsdevMountSaveDataReadOnly(kSaveDevice, titleId, s->uid)
                               : fsdevMountSaveData(kSaveDevice, titleId, s->uid);
    if (R_FAILED(rc)) {
        err = std::string(tr("Nie można otworzyć zapisu gry (kod ", "Cannot open the game save (code ")) + resultCode(rc) +
              tr("). Zamknij grę (HOME, potem X) i spróbuj ponownie.", "). Close the game (HOME, then X) and try again.");
        logf("mount save %016llX (%s) failed: 0x%X", static_cast<unsigned long long>(titleId), readOnly ? "ro" : "rw", rc);
        return false;
    }
    g_mounted = true;
    root = std::string(kSaveDevice) + ":/";
    return true;
}

bool liveCommit(std::string& err) {
    if (!g_mounted) {
        err = tr("Zapis gry nie jest otwarty.", "The game save is not open.");
        return false;
    }
    const Result rc = fsdevCommitDevice(kSaveDevice);
    if (R_FAILED(rc)) {
        err = std::string(tr("Nie udało się zatwierdzić zapisu gry (kod ", "Could not commit the game save (code ")) +
              resultCode(rc) + ").";
        logf("commit failed: 0x%X", rc);
        return false;
    }
    return true;
}

void liveUnmount() {
    if (!g_mounted) return;
    fsdevUnmountDevice(kSaveDevice);
    g_mounted = false;
}

std::vector<uint8_t> liveMetaFile(uint64_t titleId, const std::string& userId) {
    std::vector<uint8_t> out;
    const SaveRec* s = findSave(titleId, userId);
    if (!s) return out;
    FsSaveDataExtraData extra;
    std::memset(&extra, 0, sizeof(extra));
    if (R_FAILED(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
            &extra, sizeof(extra), static_cast<FsSaveDataSpaceId>(s->spaceId), s->saveId)))
        return out;
    // JKSV fs::SaveMetaData (packed, little endian)
    auto put = [&out](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    };
    const uint32_t magic = 0x56534B4A;
    const uint8_t revision = 1;
    put(&magic, 4);
    put(&revision, 1);
    put(&extra.attr.application_id, 8);
    put(&extra.attr.uid, 16);
    put(&extra.attr.system_save_data_id, 8);
    put(&extra.attr.save_data_type, 1);
    put(&extra.attr.save_data_rank, 1);
    put(&extra.attr.save_data_index, 2);
    put(&extra.owner_id, 8);
    put(&extra.timestamp, 8);
    put(&extra.flags, 4);
    put(&extra.data_size, 8);
    put(&extra.journal_size, 8);
    put(&extra.commit_id, 8);
    put(&s->spaceId, 1);
    return out;
}

bool networkAvailable() {
    if (!g_nifmReady) g_nifmReady = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    if (!g_nifmReady) return false;
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus status;
    return R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
           status == NifmInternetConnectionStatus_Connected;
}

bool httpGet(const std::string& url, std::vector<uint8_t>& out, std::string& err,
             const std::function<bool(uint64_t, uint64_t)>& progress) {
    out.clear();
    if (!initNetwork(err)) return false;
    CURL* curl = curl_easy_init();
    if (!curl) {
        err = tr("Nie można uruchomić biblioteki sieciowej.", "Cannot start the network library.");
        return false;
    }
    HttpContext ctx{&out, &progress};
    curl_slist* headers = nullptr;
    if (url.compare(0, 23, "https://api.github.com/") == 0) {
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SwitchModHub/" APP_VERSION_STR);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, httpProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 64L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    if (rc != CURLE_OK) {
        err = std::string(tr("Błąd sieci: ", "Network error: ")) + curl_easy_strerror(rc);
        logf("http %s: %s", url.c_str(), err.c_str());
        return false;
    }
    if (status >= 400) {
        err = "HTTP " + std::to_string(status);
        logf("http %s: %ld", url.c_str(), status);
        return false;
    }
    return true;
}

std::string selfPath() { return g_selfPath; }

bool setNextLoad(const std::string& nroPath) {
    if (!envHasNextLoad()) return false;
    const std::string args = "\"" + nroPath + "\"";
    return R_SUCCEEDED(envSetNextLoad(nroPath.c_str(), args.c_str()));
}

} // namespace plat

#endif // __SWITCH__
