// key_log.cpp: optional log of the keys the window sees, to diagnose keyboard input.
//
// Why
//   On a Windows PC the keyboard did nothing in the game (findings/20 s.16)
//   while the same build ran normally. The pad stubs read the keyboard with
//   raylib's IsKeyDown, whose state the window thread updates when it polls
//   its events (EndDrawing). This log shows whether raylib sees the keys at
//   all, and whether the window has the keyboard focus.
//
// What this file does
//   Only when the environment variable BEN10_KEY_LOG is set (to anything),
//   it starts a host thread that, every 100 ms, reads the keys the pad
//   stubs map (Enter, X, Z, C, V, W, A, S, D, Q, E, both Shifts, Space,
//   arrows) and the window focus, and prints a line to stderr whenever they
//   change:
//     [keys] focused=1 down: ENTER X
//   Without the variable nothing is started and nothing changes. Reading
//   raylib's key state from another thread is what the pad stubs already
//   do on the game thread.

#include "ps2_runtime.h"
#include "game_overrides.h"
#include "raylib.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace ben10::key_log
{
    struct NamedKey
    {
        int key;
        const char *name;
    };

    constexpr NamedKey kKeys[] = {
        {KEY_ENTER, "ENTER"}, {KEY_X, "X"}, {KEY_Z, "Z"}, {KEY_C, "C"}, {KEY_V, "V"},
        {KEY_W, "W"}, {KEY_A, "A"}, {KEY_S, "S"}, {KEY_D, "D"}, {KEY_Q, "Q"}, {KEY_E, "E"},
        {KEY_LEFT_SHIFT, "LSHIFT"}, {KEY_RIGHT_SHIFT, "RSHIFT"}, {KEY_SPACE, "SPACE"},
        {KEY_UP, "UP"}, {KEY_DOWN, "DOWN"}, {KEY_LEFT, "LEFT"}, {KEY_RIGHT, "RIGHT"},
    };

    void poll()
    {
        std::string last;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!IsWindowReady())
                continue;
            std::string now = IsWindowFocused() ? "focused=1 down:" : "focused=0 down:";
            for (const NamedKey &k : kKeys)
                if (IsKeyDown(k.key))
                    (now += ' ') += k.name;
            if (now != last)
            {
                std::fprintf(stderr, "[keys] %s\n", now.c_str());
                last = now;
            }
        }
    }

    void apply(PS2Runtime &)
    {
        if (std::getenv("BEN10_KEY_LOG") == nullptr)
            return;
        std::thread(poll).detach();
        std::fprintf(stderr, "[keys] key log on (BEN10_KEY_LOG)\n");
    }
}

PS2_REGISTER_GAME_OVERRIDE("Key log (BEN10_KEY_LOG)", "SLUS_216.61", 0x00100008u, 0u, &ben10::key_log::apply);
