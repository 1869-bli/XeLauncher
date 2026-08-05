#pragma once
#include <windows.h>

// Polls a gamepad through the system joystick driver (winmm joyGetPosEx) so a
// PS4/PS5/Switch pad feeds ImGui gamepad keys. Used as a fallback when no
// XInput controller is present (XInput pads are handled by the ImGui Win32
// backend directly).
class GamepadPoll {
public:
    GamepadPoll();
    ~GamepadPoll();
    void init(HINSTANCE hInstance, HWND hwnd);
    void newFrame(bool xinputActive);
    void shutdown();
    bool connected() const;

    struct Debug {
        bool present = false;
        bool xinputActive = false;
        DWORD buttons = 0;
        DWORD pov = 0;
        int lastError = 0;
        DWORD x = 0, y = 0, z = 0, r = 0;
    };
    Debug debug() const;

private:
    struct Impl;
    Impl* impl = nullptr;
};
