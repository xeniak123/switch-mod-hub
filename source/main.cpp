// Switch Mod Hub - Nintendo Switch homebrew entry point.
#ifdef __SWITCH__

#include <switch.h>

#include <cstdio>

#include "app.h"
#include "fsutil.h"
#include "gfx.h"
#include "log.h"
#include "platform.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

static void consoleError(const char* message) {
    consoleInit(nullptr);
    std::printf("\n  Switch Mod Hub %s\n\n  %s\n\n  Nacisnij + aby wyjsc.\n", APP_VERSION_STR, message);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

int main(int argc, char** argv) {
    plat::setArgs(argc, argv);
    fsu::mkdirs("sdmc:/switch/SwitchModHub");
    logInit("sdmc:/switch/SwitchModHub/log.txt");
    logf("=== Switch Mod Hub %s (applet mode: %d, %s)", APP_VERSION_STR, plat::isAppletMode() ? 1 : 0,
         plat::selfPath().c_str());

    if (!plat::init()) {
        logf("platform init failed");
        consoleError("Nie udalo sie zainicjowac ekranu.");
        return 0;
    }

    App app;
    if (!app.init()) {
        logf("no usable system font");
        plat::shutdown();
        consoleError("Nie udalo sie wczytac czcionki systemowej.");
        return 0;
    }

    Canvas canvas;
    while (plat::running() && !app.wantsExit()) {
        const plat::Input in = plat::pollInput();
        app.update(in);
        if (app.wantsExit()) break;
        if (!app.needsRedraw()) {
            // nothing changed: keep the last frame on screen and save CPU / battery
            svcSleepThread(16666667LL);
            continue;
        }
        int w = 0, h = 0, stride = 0;
        uint32_t* px = plat::beginFrame(w, h, stride);
        canvas.attach(px, w, h, stride);
        app.draw(canvas);
        plat::endFrame();
    }

    logf("exit");
    plat::shutdown();
    return 0;
}

#endif // __SWITCH__
