// PC implementation of the platform layer used by the automated tests.
// Frames go to an in-memory framebuffer, input comes from a script, the SD card, the console's
// save data, the installed games and the internet are folders and files.
#include "host_platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "../source/fsutil.h"
#include "../source/json.h"
#include "../source/platform.h"

namespace host {
std::vector<uint32_t> framebuffer(1280 * 720);
uint64_t clockMs = 1000;
std::deque<std::string> textAnswers;
std::vector<std::string> textRequests;
plat::Input nextInput;
std::string sdRoot = "/tmp/sse_sd/";
std::string stamp = "2026-10-27 @ 12.34.56";
std::vector<std::vector<uint8_t>> fontFiles;
std::string gamesFile;
std::string liveRoot;
bool liveFailCommit = false;
uint64_t runningTid = 0;
std::string httpRoot;
std::vector<std::string> httpLog;
std::string selfNro;
std::string nextLoad;
std::string liveMounted;  // "<title id>/<user>"
} // namespace host

namespace {

std::string hex16(uint64_t v) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(v));
    return buf;
}

bool parseHex16(const std::string& s, uint64_t& v) {
    if (s.size() != 16) return false;
    char* end = nullptr;
    v = std::strtoull(s.c_str(), &end, 16);
    return end && *end == '\0';
}

std::string stagingDir() { return host::liveRoot.substr(0, host::liveRoot.size() - 1) + ".mount"; }

} // namespace

namespace plat {

void setArgs(int, char**) {}

bool init() {
    const char* list = std::getenv("SSE_FONTS");
    std::string fonts = list ? list : "/mnt/c/Windows/Fonts/segoeui.ttf";
    size_t start = 0;
    while (start <= fonts.size()) {
        size_t end = fonts.find(':', start);
        if (end == std::string::npos) end = fonts.size();
        const std::string path = fonts.substr(start, end - start);
        if (!path.empty()) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f) {
                std::vector<uint8_t> data;
                uint8_t buf[65536];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
                std::fclose(f);
                host::fontFiles.push_back(std::move(data));
            } else {
                std::fprintf(stderr, "font not found: %s\n", path.c_str());
            }
        }
        start = end + 1;
    }
    if (const char* root = std::getenv("SSE_SDROOT")) host::sdRoot = root;
    if (!host::sdRoot.empty() && host::sdRoot.back() != '/') host::sdRoot += '/';
    if (const char* st = std::getenv("SSE_STAMP")) host::stamp = st;
    if (const char* g = std::getenv("SSE_GAMES")) host::gamesFile = g;
    if (const char* lr = std::getenv("SSE_LIVEROOT")) host::liveRoot = lr;
    if (!host::liveRoot.empty() && host::liveRoot.back() != '/') host::liveRoot += '/';
    host::liveFailCommit = std::getenv("SSE_LIVE_FAILCOMMIT") != nullptr;
    if (const char* r = std::getenv("SSE_RUNNING_TID")) parseHex16(r, host::runningTid);
    if (const char* h = std::getenv("SSE_HTTP_ROOT")) host::httpRoot = h;
    if (!host::httpRoot.empty() && host::httpRoot.back() != '/') host::httpRoot += '/';
    host::selfNro = host::sdRoot + "switch/SwitchModHub/SwitchModHub.nro";
    if (const char* s = std::getenv("SSE_SELF")) host::selfNro = s;
    return true;
}

void shutdown() {}
bool running() { return true; }
Input pollInput() { return host::nextInput; }

uint32_t* beginFrame(int& width, int& height, int& stridePixels) {
    width = 1280;
    height = 720;
    stridePixels = 1280;
    return host::framebuffer.data();
}

void endFrame() { host::clockMs += 16; }
uint64_t nowMs() { return host::clockMs; }

bool textInput(const TextRequest& req, std::string& out) {
    host::textRequests.push_back(req.header + " | initial=" + req.initial + (req.numeric ? " | numeric" : ""));
    if (host::textAnswers.empty()) return false;
    const std::string a = host::textAnswers.front();
    host::textAnswers.pop_front();
    if (a == "<cancel>") return false;
    out = a;
    return true;
}

std::vector<FontBlob> fonts() {
    std::vector<FontBlob> r;
    for (const auto& f : host::fontFiles) {
        FontBlob b;
        b.data = f.data();
        b.size = f.size();
        r.push_back(b);
    }
    return r;
}

std::string sdPath(const std::string& sdmcPath) {
    if (sdmcPath.compare(0, 6, "sdmc:/") == 0) return host::sdRoot + sdmcPath.substr(6);
    return sdmcPath;
}

std::string localTimeStamp() { return host::stamp; }

std::string formatTime(int64_t t) {
    if (t <= 0) return "";
    const time_t tt = static_cast<time_t>(t);
    struct tm tmv;
    localtime_r(&tt, &tmv);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

bool isAppletMode() { return false; }

bool listGames(std::vector<GameInfo>& out, std::string& err) {
    out.clear();
    if (host::gamesFile.empty()) return true;
    std::vector<uint8_t> data;
    if (!fsu::readFile(host::gamesFile, data, err)) return false;
    json::Value root;
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(reinterpret_cast<const char*>(data.data()), data.size(), root, end, pe)) {
        err = "games.json: " + pe.message;
        return false;
    }
    for (const json::Value& g : root.items) {
        const json::Value* id = g.find("titleId");
        const json::Value* name = g.find("name");
        GameInfo info;
        if (!id || !name || !parseHex16(id->text, info.titleId)) continue;
        info.name = name->text;
        out.push_back(info);
    }
    return true;
}

std::vector<uint64_t> titlesWithSaves() {
    std::vector<uint64_t> out;
    std::vector<fsu::Entry> entries;
    if (host::liveRoot.empty() || !fsu::listDir(host::liveRoot, entries)) return out;
    for (const fsu::Entry& e : entries) {
        uint64_t tid = 0;
        if (e.isDir && parseHex16(e.name, tid)) out.push_back(tid);
    }
    std::sort(out.begin(), out.end());
    return out;
}

uint64_t runningGame() { return host::runningTid; }

bool liveUsers(uint64_t titleId, std::vector<SaveUser>& out, std::string&) {
    out.clear();
    if (host::liveRoot.empty()) return true;
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(host::liveRoot + hex16(titleId), entries)) return true;
    std::sort(entries.begin(), entries.end(), [](const fsu::Entry& a, const fsu::Entry& b) { return a.name < b.name; });
    for (const fsu::Entry& e : entries) {
        if (!e.isDir || e.name.empty() || e.name[0] == '.') continue;
        SaveUser u;
        u.id = e.name;
        u.name = e.name;
        out.push_back(u);
    }
    return true;
}

bool liveMount(uint64_t titleId, const std::string& userId, bool, std::string& root, std::string& err) {
    liveUnmount();
    const std::string rel = hex16(titleId) + "/" + userId;
    const std::string src = host::liveRoot + rel;
    if (host::liveRoot.empty() || !fsu::isDir(src)) {
        err = "no such save: " + rel;
        return false;
    }
    fsu::removeTree(stagingDir());
    if (!fsu::copyDir(src, stagingDir(), err)) return false;
    host::liveMounted = rel;
    root = stagingDir() + "/";
    return true;
}

bool liveCommit(std::string& err) {
    if (host::liveMounted.empty()) {
        err = "not mounted";
        return false;
    }
    if (host::liveFailCommit) {
        err = "simulated commit failure";
        return false;
    }
    const std::string dst = host::liveRoot + host::liveMounted;
    fsu::removeTree(dst);
    return fsu::copyDir(stagingDir(), dst, err);
}

void liveUnmount() {
    if (host::liveMounted.empty()) return;
    fsu::removeTree(stagingDir());
    host::liveMounted.clear();
}

std::vector<uint8_t> liveMetaFile(uint64_t titleId, const std::string& userId) {
    std::vector<uint8_t> m(86, 0);
    const uint32_t magic = 0x56534B4A;
    for (int i = 0; i < 4; ++i) m[static_cast<size_t>(i)] = static_cast<uint8_t>(magic >> (8 * i));
    m[4] = 1;
    for (int i = 0; i < 8; ++i) m[5 + static_cast<size_t>(i)] = static_cast<uint8_t>(titleId >> (8 * i));
    for (size_t i = 0; i < userId.size() && i < 16; ++i) m[13 + i] = static_cast<uint8_t>(userId[i]);
    return m;
}

bool networkAvailable() { return !host::httpRoot.empty(); }

bool httpGet(const std::string& url, std::vector<uint8_t>& out, std::string& err,
             const std::function<bool(uint64_t, uint64_t)>& progress) {
    out.clear();
    host::httpLog.push_back(url);
    if (host::httpRoot.empty()) {
        err = "offline";
        return false;
    }
    if (url.compare(0, 8, "https://") != 0 || url.find("..") != std::string::npos) {
        err = "bad url";
        return false;
    }
    std::string e2;
    if (!fsu::readFile(host::httpRoot + url.substr(8), out, e2)) {
        err = "HTTP 404";
        return false;
    }
    if (progress && !progress(out.size(), out.size())) {
        err = "cancelled";
        return false;
    }
    return true;
}

std::string selfPath() { return host::selfNro; }

bool setNextLoad(const std::string& nroPath) {
    host::nextLoad = nroPath;
    return true;
}

} // namespace plat
