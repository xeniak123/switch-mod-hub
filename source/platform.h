// Platform layer: implemented for the Switch (platform_switch.cpp, libnx) and for the
// PC test harness (tests/host_platform.cpp). Everything else is platform independent.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace plat {

enum Button : uint32_t {
    BTN_A = 1u << 0,
    BTN_B = 1u << 1,
    BTN_X = 1u << 2,
    BTN_Y = 1u << 3,
    BTN_L = 1u << 4,
    BTN_R = 1u << 5,
    BTN_ZL = 1u << 6,
    BTN_ZR = 1u << 7,
    BTN_PLUS = 1u << 8,
    BTN_MINUS = 1u << 9,
    BTN_UP = 1u << 10,
    BTN_DOWN = 1u << 11,
    BTN_LEFT = 1u << 12,
    BTN_RIGHT = 1u << 13,
};

struct Input {
    uint32_t down = 0;  // pressed this frame
    uint32_t held = 0;  // currently held
    bool touching = false;       // a finger is on the touch screen
    int touchX = 0, touchY = 0;  // touch position in screen pixels (1280 x 720)
};

struct TextRequest {
    std::string header;
    std::string guide;
    std::string initial;
    bool numeric = false;
    bool allowNegative = false;
    bool allowDecimal = false;
    int maxLength = 256;
};

struct FontBlob {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

// Command line of the app (argv[0] is the path of the running .nro). Call before init().
void setArgs(int argc, char** argv);
bool init();
void shutdown();
bool running();
Input pollInput();
uint32_t* beginFrame(int& width, int& height, int& stridePixels);
void endFrame();
uint64_t nowMs();
// Shows the system keyboard. Returns false if cancelled or unavailable.
bool textInput(const TextRequest& req, std::string& out);
// Font files, primary first; the rest are fallbacks for missing glyphs.
std::vector<FontBlob> fonts();
// Maps an "sdmc:/..." path to the real path on this platform.
std::string sdPath(const std::string& sdmcPath);
// Local date and time, e.g. "2026-10-27 @ 14.05.09".
std::string localTimeStamp();
// File time (POSIX seconds) as local "2026-10-27 14:05", or "" when unknown.
std::string formatTime(int64_t posixTime);
bool isAppletMode();

// ---- installed games ----

struct GameInfo {
    uint64_t titleId = 0;
    std::string name;
    std::vector<uint8_t> iconJpeg;  // may be empty
};

// Installed games with their names and icons.
bool listGames(std::vector<GameInfo>& out, std::string& err);
// Title ids that have user save data on the console.
std::vector<uint64_t> titlesWithSaves();
// Title id of the game running in the background (0 = none).
uint64_t runningGame();

// ---- the games' own save data on the console (editing without JKSV) ----

struct SaveUser {
    std::string id;    // opaque console user id
    std::string name;  // nickname
};

// Console users that have save data of the game. An empty list with true = no save data.
bool liveUsers(uint64_t titleId, std::vector<SaveUser>& out, std::string& err);
// Mounts the save data; root receives a path prefix such as "ssesave:/".
bool liveMount(uint64_t titleId, const std::string& userId, bool readOnly, std::string& root, std::string& err);
// Makes the writes since mounting permanent. Unmounting without a commit discards them.
bool liveCommit(std::string& err);
void liveUnmount();
// Contents of a JKSV ".nx_save_meta.bin" describing this save data (empty if unavailable).
std::vector<uint8_t> liveMetaFile(uint64_t titleId, const std::string& userId);

// ---- network ----

bool networkAvailable();
// HTTPS GET with redirects. progress(received, total) may return false to cancel.
bool httpGet(const std::string& url, std::vector<uint8_t>& out, std::string& err,
             const std::function<bool(uint64_t, uint64_t)>& progress = nullptr);

// ---- self update ----

// Path of the running .nro.
std::string selfPath();
// Starts this .nro when the app exits (Homebrew Menu). False if not supported.
bool setNextLoad(const std::string& nroPath);

} // namespace plat
