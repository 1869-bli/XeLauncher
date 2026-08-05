#include "gamepad.h"

#include <imgui.h>
#include <mmsystem.h>

#include <cmath>
#include <cstring>

#pragma comment(lib, "winmm.lib")

// Polls the system joystick driver (joyGetPosEx) so that any HID gamepad
// (PS4/PS5/Switch etc.) feeds ImGui gamepad keys. Used as a fallback when no
// XInput controller is present (XInput pads are handled by the ImGui Win32
// backend directly). The DualSense reports 15 buttons on page 0x09 in Sony
// order: 1=Square 2=Cross 3=Circle 4=Triangle 5=L1 6=R1 7=L2 8=R2
// 9=Share 10=Options 11=L3 12=R3, with sticks on X/Y/Z/R and D-pad on POV.

// Key/axis state is fed every frame rather than on transitions only, so a
// release is never missed (e.g. when feeding is paused while a game runs).
// AddKeyEvent/AddKeyAnalogEvent filter duplicate submissions, making this cheap.

struct GamepadPoll::Impl {
    UINT id = JOYSTICKID1;
    HWND hwnd = nullptr;
    bool present = false;
    bool xinputActive = false;
    int frameCount = 0;
    DWORD lastButtons = 0;
    DWORD lastPov = 0;
    DWORD lastAxes[4] = {0, 0, 0, 0};
    int lastError = 0;

    // Auto-calibrated stick centers. Rather than assuming a fixed center
    // (32767) we capture the neutral position when the pad connects and then
    // slowly re-track it, so stick drift / a non-centered rest reading can
    // never hold a direction key down (which ImGui turns into constant scroll).
    float center[4] = {32767.0f, 32767.0f, 32767.0f, 32767.0f};
    bool calibDone = false;
    int calibSamples = 0;
    int calibSum[4] = {0, 0, 0, 0};

    void feed(ImGuiKey key, bool down);
    void feedStick(int idx, int axis, ImGuiKey neg, ImGuiKey pos, ImGuiKey dpadNeg, ImGuiKey dpadPos);
    void updateCenter();
    void releaseAll();
};

GamepadPoll::GamepadPoll() : impl(new Impl) {}
GamepadPoll::~GamepadPoll() {
    shutdown();
    delete impl;
}

void GamepadPoll::init(HINSTANCE hInstance, HWND hwnd) {
    (void)hInstance;
    impl->hwnd = hwnd;
}

static bool probe(UINT id) {
    JOYINFOEX info;
    ZeroMemory(&info, sizeof(info));
    info.dwSize = sizeof(info);
    info.dwFlags = JOY_RETURNBUTTONS;
    return joyGetPosEx(id, &info) == JOYERR_NOERROR;
}

void GamepadPoll::newFrame(bool xinputActive, bool appActive) {
    Impl* p = impl;
    p->xinputActive = xinputActive;
    // While a game (xenia) is running the launcher must not react to the
    // gamepad at all, even if it happens to be the foreground window.
    if (xinputActive || !appActive) {
        p->present = false;
        p->releaseAll();
        return;
    }
    // Only feed while the launcher is the foreground app.
    if (impl->hwnd && GetForegroundWindow() != impl->hwnd) {
        p->present = false;
        p->releaseAll();
        return;
    }
    p->frameCount++;

    // Re-probe for a connected gamepad a couple of times a second.
    if (p->frameCount % 60 == 1) p->present = probe(p->id);
    if (!p->present) {
        p->releaseAll();
        return;
    }

    JOYINFOEX info;
    ZeroMemory(&info, sizeof(info));
    info.dwSize = sizeof(info);
    info.dwFlags = JOY_RETURNALL;
    p->lastError = joyGetPosEx(p->id, &info);
    if (p->lastError != JOYERR_NOERROR) {
        p->present = false;
        p->releaseAll();
        return;
    }
    p->lastButtons = info.dwButtons;
    p->lastPov = info.dwPOV;
    p->lastAxes[0] = info.dwXpos;
    p->lastAxes[1] = info.dwYpos;
    p->lastAxes[2] = info.dwZpos;
    p->lastAxes[3] = info.dwRpos;
    p->updateCenter();

    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    DWORD b = info.dwButtons;
    p->feed(ImGuiKey_GamepadFaceLeft, b & 0x0001);   // Square
    p->feed(ImGuiKey_GamepadFaceDown, b & 0x0002);   // Cross
    p->feed(ImGuiKey_GamepadFaceRight, b & 0x0004);  // Circle
    p->feed(ImGuiKey_GamepadFaceUp, b & 0x0008);     // Triangle
    p->feed(ImGuiKey_GamepadL1, b & 0x0010);
    p->feed(ImGuiKey_GamepadR1, b & 0x0020);
    p->feed(ImGuiKey_GamepadL2, b & 0x0040);
    p->feed(ImGuiKey_GamepadR2, b & 0x0080);
    p->feed(ImGuiKey_GamepadBack, b & 0x0100);       // Share
    p->feed(ImGuiKey_GamepadStart, b & 0x0200);      // Options
    p->feed(ImGuiKey_GamepadL3, b & 0x0400);
    p->feed(ImGuiKey_GamepadR3, b & 0x0800);

    p->feedStick(0, (int)info.dwXpos, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
                 ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight);
    p->feedStick(1, (int)info.dwYpos, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
                 ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown);
    p->feedStick(2, (int)info.dwZpos, ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight,
                 ImGuiKey_None, ImGuiKey_None);
    p->feedStick(3, (int)info.dwRpos, ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown,
                 ImGuiKey_None, ImGuiKey_None);

    // Physical D-pad (POV). Only override the stick-mirrored D-pad state when
    // the D-pad is actually pressed, so a centered POV doesn't cancel the
    // left-stick mirror fed above.
    DWORD pov = info.dwPOV;
    if (pov != JOY_POVCENTERED) {
        if (pov >= 36000) pov = 0;
        p->feed(ImGuiKey_GamepadDpadUp, (pov < 4500) || (pov > 31500));
        p->feed(ImGuiKey_GamepadDpadRight, (pov > 4500) && (pov < 13500));
        p->feed(ImGuiKey_GamepadDpadDown, (pov > 13500) && (pov < 22500));
        p->feed(ImGuiKey_GamepadDpadLeft, (pov > 22500) && (pov < 31500));
    }
}

void GamepadPoll::Impl::feed(ImGuiKey key, bool down) {
    if (key == ImGuiKey_None) return;
    ImGui::GetIO().AddKeyEvent(key, down);
}

void GamepadPoll::Impl::updateCenter() {
    // First frames after connect: capture the neutral position as the average
    // of ~0.75s of samples. Fixes pads whose rest reading is far from 32767.
    static const int kSamples = 45;
    if (!calibDone) {
        if (calibSamples < kSamples) {
            for (int i = 0; i < 4; i++) calibSum[i] += lastAxes[i];
            calibSamples++;
        } else {
            for (int i = 0; i < 4; i++) center[i] = (float)(calibSum[i] / kSamples);
            calibDone = true;
        }
    }
    // Slow re-track: only update the center while the axis sits near it, so a
    // sustained stick push can never drag the center onto the pushed position.
    for (int i = 0; i < 4; i++) {
        const float a = (float)lastAxes[i];
        if (std::fabs(a - center[i]) < 13107.0f)
            center[i] += (a - center[i]) * 0.02f;
        center[i] = center[i] < 13107.0f ? 13107.0f : (center[i] > (float)(65535 - 13107) ? (float)(65535 - 13107) : center[i]);
    }
}

void GamepadPoll::Impl::feedStick(int idx, int axis, ImGuiKey neg, ImGuiKey pos,
                                  ImGuiKey dpadNeg, ImGuiKey dpadPos) {
    // Axes are 0-65535; the effective center is the auto-calibrated neutral.
    // 20% deadzone; magnitude ramps 0..1 to edge.
    static const int dz = 13107;  // 65536 * 0.20
    const float c = center[idx];
    const float range = (float)(32767 - dz);
    float nm = 0.0f, pm = 0.0f;
    if (c - axis > dz)
        nm = (c - axis - dz) / range < 1.0f ? (c - axis - dz) / range : 1.0f;
    else if (axis - c > dz)
        pm = (axis - c - dz) / range < 1.0f ? (axis - c - dz) / range : 1.0f;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyAnalogEvent(neg, nm > 0.0f, nm);
    io.AddKeyAnalogEvent(pos, pm > 0.0f, pm);
    // Mirror the stick onto the D-pad (nav moves only react to Dpad keys).
    // 50% stick travel engages the corresponding D-pad direction.
    if (dpadNeg != ImGuiKey_None) feed(dpadNeg, nm > 0.5f);
    if (dpadPos != ImGuiKey_None) feed(dpadPos, pm > 0.5f);
}

void GamepadPoll::Impl::releaseAll() {
    if (ImGui::GetCurrentContext() == nullptr) return;  // e.g. after DestroyContext
    ImGuiIO& io = ImGui::GetIO();
    static const ImGuiKey keys[] = {
        ImGuiKey_GamepadFaceLeft, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceUp,
        ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown,
        ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadL1,
        ImGuiKey_GamepadR1, ImGuiKey_GamepadL2, ImGuiKey_GamepadR2,
        ImGuiKey_GamepadL3, ImGuiKey_GamepadR3, ImGuiKey_GamepadBack,
        ImGuiKey_GamepadStart,
        ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickLeft,
        ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown,
        ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight,
    };
    for (ImGuiKey k : keys)
        io.AddKeyEvent(k, false);
}

void GamepadPoll::shutdown() {
    if (!impl) return;
    impl->present = false;
    impl->releaseAll();
}

bool GamepadPoll::connected() const {
    return impl && impl->present;
}

GamepadPoll::Debug GamepadPoll::debug() const {
    Debug d;
    if (!impl) return d;
    d.present = impl->present;
    d.xinputActive = impl->xinputActive;
    d.buttons = impl->lastButtons;
    d.pov = impl->lastPov;
    d.lastError = impl->lastError;
    d.x = impl->lastAxes[0];
    d.y = impl->lastAxes[1];
    d.z = impl->lastAxes[2];
    d.r = impl->lastAxes[3];
    d.cx = impl->center[0];
    d.cy = impl->center[1];
    d.cz = impl->center[2];
    d.cr = impl->center[3];
    return d;
}
