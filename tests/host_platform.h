// Test hooks of the PC platform layer.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "../source/platform.h"

namespace host {
extern std::vector<uint32_t> framebuffer;
extern uint64_t clockMs;
extern std::deque<std::string> textAnswers;   // answers for the next keyboard prompts
extern std::vector<std::string> textRequests; // log of keyboard prompts
extern plat::Input nextInput;
extern std::string sdRoot;
extern std::string stamp;
// Installed games: JSON file [{"titleId":"0100...","name":"..."}] (SSE_GAMES).
extern std::string gamesFile;
// Fake console save data: <liveRoot>/<title id>/<user>/... (folder name = user id = nickname).
// Mounting copies it to "<liveRoot>.mount", a commit copies it back, unmounting discards it.
extern std::string liveRoot;
extern bool liveFailCommit;   // simulate a failing commit (SSE_LIVE_FAILCOMMIT=1)
extern uint64_t runningTid;   // game running in the background (SSE_RUNNING_TID=<hex>)
// Fake internet: https://host/path is read from <httpRoot>/host/path (SSE_HTTP_ROOT); empty = offline.
extern std::string httpRoot;
extern std::vector<std::string> httpLog;
extern std::string selfNro;   // path of "the running .nro" (SSE_SELF)
extern std::string nextLoad;  // what setNextLoad() asked for
} // namespace host
