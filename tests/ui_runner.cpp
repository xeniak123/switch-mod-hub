// Drives the real App with a scripted controller and writes screenshots (PNG).
// Script commands (one per line, '#' = comment):
//   frames N            run N frames without input
//   press BTN [BTN...]  press buttons for one frame, then release
//   hold BTN N          hold a button for N frames, then release
//   text STRING         answer for the next keyboard prompt ("<cancel>" cancels)
//   shot NAME           draw a frame and save NAME.png
//   tap X Y             touch the screen at X,Y and release
//   drag X Y DY STEPS   touch at X,Y, move by DY pixels in STEPS frames, release
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "../source/app.h"
#include "../source/fsutil.h"
#include "../source/gfx.h"
#include "../source/log.h"
#include "host_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace {

uint32_t buttonMask(const std::string& name) {
    static const std::pair<const char*, uint32_t> map[] = {
        {"A", plat::BTN_A},      {"B", plat::BTN_B},       {"X", plat::BTN_X},         {"Y", plat::BTN_Y},
        {"L", plat::BTN_L},      {"R", plat::BTN_R},       {"ZL", plat::BTN_ZL},       {"ZR", plat::BTN_ZR},
        {"PLUS", plat::BTN_PLUS}, {"MINUS", plat::BTN_MINUS}, {"UP", plat::BTN_UP},    {"DOWN", plat::BTN_DOWN},
        {"LEFT", plat::BTN_LEFT}, {"RIGHT", plat::BTN_RIGHT},
    };
    for (const auto& m : map)
        if (name == m.first) return m.second;
    std::fprintf(stderr, "unknown button %s\n", name.c_str());
    return 0;
}

App* g_app = nullptr;
Canvas g_canvas;
bool g_exited = false;

void runFrame(const plat::Input& in) {
    if (g_exited) return;
    host::nextInput = in;
    plat::Input polled = plat::pollInput();
    g_app->update(polled);
    if (g_app->wantsExit()) {
        g_exited = true;
        return;
    }
    int w, h, s;
    uint32_t* px = plat::beginFrame(w, h, s);
    g_canvas.attach(px, w, h, s);
    g_app->draw(g_canvas);
    plat::endFrame();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ui_runner script.txt outdir\n");
        return 2;
    }
    plat::init();
    fsu::mkdirs(plat::sdPath("sdmc:/switch/SwitchModHub"));
    logInit(plat::sdPath("sdmc:/switch/SwitchModHub/log.txt"));
    App app;
    if (!app.init()) {
        std::fprintf(stderr, "app init failed (font?)\n");
        return 1;
    }
    g_app = &app;
    const std::string outDir = argv[2];
    fsu::mkdirs(outDir);

    std::ifstream script(argv[1]);
    std::string line;
    int lineNo = 0;
    runFrame(plat::Input());
    while (std::getline(script, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd == "frames") {
            int n = 1;
            ss >> n;
            for (int i = 0; i < n; ++i) runFrame(plat::Input());
        } else if (cmd == "press") {
            plat::Input in;
            std::string b;
            while (ss >> b) in.down |= buttonMask(b);
            in.held = in.down;
            runFrame(in);
            runFrame(plat::Input());
        } else if (cmd == "hold") {
            std::string b;
            int n = 1;
            ss >> b >> n;
            const uint32_t m = buttonMask(b);
            for (int i = 0; i < n; ++i) {
                plat::Input in;
                in.held = m;
                if (i == 0) in.down = m;
                runFrame(in);
            }
            runFrame(plat::Input());
        } else if (cmd == "tap") {
            plat::Input in;
            in.touching = true;
            ss >> in.touchX >> in.touchY;
            runFrame(in);
            runFrame(plat::Input());
        } else if (cmd == "drag") {
            int x = 0, y = 0, dy = 0, steps = 10;
            ss >> x >> y >> dy >> steps;
            for (int i = 0; i <= steps; ++i) {
                plat::Input in;
                in.touching = true;
                in.touchX = x;
                in.touchY = y + dy * i / steps;
                runFrame(in);
            }
            runFrame(plat::Input());
        } else if (cmd == "text") {
            std::string rest;
            std::getline(ss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            host::textAnswers.push_back(rest);
        } else if (cmd == "bench") {
            int n = 100;
            std::string label;
            ss >> n >> label;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i) runFrame(plat::Input());
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / n;
            std::printf("bench %-14s %.3f ms/frame\n", label.c_str(), ms);
        } else if (cmd == "shot") {
            std::string name;
            ss >> name;
            runFrame(plat::Input());
            const std::string path = outDir + "/" + name + ".png";
            if (!stbi_write_png(path.c_str(), 1280, 720, 4, host::framebuffer.data(), 1280 * 4))
                std::fprintf(stderr, "failed to write %s\n", path.c_str());
        } else {
            std::fprintf(stderr, "line %d: unknown command %s\n", lineNo, cmd.c_str());
            return 2;
        }
        if (g_exited) break;
    }
    for (const std::string& r : host::textRequests) std::printf("keyboard: %s\n", r.c_str());
    if (!host::nextLoad.empty()) std::printf("nextLoad=%s\n", host::nextLoad.c_str());
    for (const std::string& u : host::httpLog) std::printf("http: %s\n", u.c_str());
    std::printf("exited=%d unused_text_answers=%zu\n", g_exited ? 1 : 0, host::textAnswers.size());
    return 0;
}
