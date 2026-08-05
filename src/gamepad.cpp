#include "gamepad.h"

#include <imgui.h>
#include <mmsystem.h>

#include <cstring>
#include <map>

#pragma comment(lib, "winmm.lib")

// Polls the system joystick driver (joyGetPosEx) so that any HID gamepad
// (PS4/PS5/Switch etc.) feeds ImGui gamepad keys. Used as a fallback when no
// XInput controller is present (XInput pads are handled by the ImGui Win32
// backend directly). The DualSense reports 15 buttons on page 0x09 in Sony
// order: 1=Square 2=Cross 3=Circle 4=Triangle 5=L1 6=R1 7=L2 8=R2
// 9=Share 10=Options 11=L3 12=R3, with sticks on X/Y/Z/R and D-pad on POV.

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
    std::map<ImGuiKey, bool> lastBtn;
    std::map<ImGuiKey, bool> lastAxis;

    void feed(ImGuiKey key, bool down);
    void feedBtn(ImGuiKey key, bool down);
    void feedStick(int axis, ImGuiKey neg, ImGuiKey pos, ImGuiKey dpadNeg, ImGuiKey dpadPos);
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

void GamepadPoll::newFrame(bool xinputActive) {
    Impl* p = impl;
    p->xinputActive = xinputActive;
    if (xinputActive) {
        p->present = false;
        return;
    }
    // Only feed the gamepad while the launcher is the foreground app. While a
    // game (xenia) is running the launcher keeps rendering, and without this a
    // gamepad press aimed at the game would also reach the launcher and
    // relaunch the selected game.
    if (impl->hwnd && GetForegroundWindow() != impl->hwnd) {
        p->present = false;
        return;
    }
    p->frameCount++;

    // Re-probe for a connected gamepad a couple of times a second.
    if (p->frameCount % 60 == 1) p->present = probe(p->id);
    if (!p->present) return;

    JOYINFOEX info;
    ZeroMemory(&info, sizeof(info));
    info.dwSize = sizeof(info);
    info.dwFlags = JOY_RETURNALL;
    p->lastError = joyGetPosEx(p->id, &info);
    if (p->lastError != JOYERR_NOERROR) {
        p->present = false;
        return;
    }
    p->lastButtons = info.dwButtons;
    p->lastPov = info.dwPOV;
    p->lastAxes[0] = info.dwXpos;
    p->lastAxes[1] = info.dwYpos;
    p->lastAxes[2] = info.dwZpos;
    p->lastAxes[3] = info.dwRpos;

    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    DWORD b = info.dwButtons;
    p->feedBtn(ImGuiKey_GamepadFaceLeft, b & 0x0001);   // Square
    p->feedBtn(ImGuiKey_GamepadFaceDown, b & 0x0002);   // Cross
    p->feedBtn(ImGuiKey_GamepadFaceRight, b & 0x0004);  // Circle
    p->feedBtn(ImGuiKey_GamepadFaceUp, b & 0x0008);     // Triangle
    p->feedBtn(ImGuiKey_GamepadL1, b & 0x0010);
    p->feedBtn(ImGuiKey_GamepadR1, b & 0x0020);
    p->feedBtn(ImGuiKey_GamepadL2, b & 0x0040);
    p->feedBtn(ImGuiKey_GamepadR2, b & 0x0080);
    p->feedBtn(ImGuiKey_GamepadBack, b & 0x0100);       // Share
    p->feedBtn(ImGuiKey_GamepadStart, b & 0x0200);      // Options
    p->feedBtn(ImGuiKey_GamepadL3, b & 0x0400);
    p->feedBtn(ImGuiKey_GamepadR3, b & 0x0800);

    p->feedStick((int)info.dwXpos, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
                 ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight);
    p->feedStick((int)info.dwYpos, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
                 ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown);
    p->feedStick((int)info.dwZpos, ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight,
                 ImGuiKey_None, ImGuiKey_None);
    p->feedStick((int)info.dwRpos, ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown,
                 ImGuiKey_None, ImGuiKey_None);

    DWORD pov = info.dwPOV;
    bool up = false, down = false, left = false, right = false;
    if (pov != JOY_POVCENTERED) {
        if (pov >= 36000) pov = 0;
        up = (pov < 4500) || (pov > 31500);
        right = (pov > 4500) && (pov < 13500);
        down = (pov > 13500) && (pov < 22500);
        left = (pov > 22500) && (pov < 31500);
    }
    p->feed(ImGuiKey_GamepadDpadUp, up);
    p->feed(ImGuiKey_GamepadDpadDown, down);
    p->feed(ImGuiKey_GamepadDpadLeft, left);
    p->feed(ImGuiKey_GamepadDpadRight, right);
}

void GamepadPoll::Impl::feed(ImGuiKey key, bool down) {
    if (key == ImGuiKey_None) return;
    ImGui::GetIO().AddKeyEvent(key, down);
}

void GamepadPoll::Impl::feedBtn(ImGuiKey key, bool down) {
    auto it = lastBtn.find(key);
    if (it != lastBtn.end() && it->second == down) return;
    lastBtn[key] = down;
    feed(key, down);
}

void GamepadPoll::Impl::feedStick(int axis, ImGuiKey neg, ImGuiKey pos,
                                  ImGuiKey dpadNeg, ImGuiKey dpadPos) {
    // 0-65535 range, center 32768. Hysteresis: engage at 50% of travel,
    // release at 75% of center.
    static const int on = 16384;   // 32768 * 0.50
    static const int off = 24576;  // 32768 * 0.75
    bool isNeg = lastAxis[neg];
    bool isPos = lastAxis[pos];
    if (axis < on) isNeg = true;
    else if (axis > off) isNeg = false;
    if (axis > 65535 - on) isPos = true;
    else if (axis < 65535 - off) isPos = false;
    if (isNeg != lastAxis[neg]) {
        lastAxis[neg] = isNeg;
        if (isNeg) {
            ImGui::GetIO().AddKeyAnalogEvent(neg, true, 1.0f);
            feedBtn(dpadNeg, true);
        } else {
            ImGui::GetIO().AddKeyAnalogEvent(neg, false, 0.0f);
            feedBtn(dpadNeg, false);
        }
    }
    if (isPos != lastAxis[pos]) {
        lastAxis[pos] = isPos;
        if (isPos) {
            ImGui::GetIO().AddKeyAnalogEvent(pos, true, 1.0f);
            feedBtn(dpadPos, true);
        } else {
            ImGui::GetIO().AddKeyAnalogEvent(pos, false, 0.0f);
            feedBtn(dpadPos, false);
        }
    }
}

void GamepadPoll::shutdown() {
    if (!impl) return;
    impl->present = false;
    impl->lastBtn.clear();
    impl->lastAxis.clear();
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
    return d;
}
