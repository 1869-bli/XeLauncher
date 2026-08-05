#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "editor.h"
#include "gamepad.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

static EditorApp g_app;
static GamepadPoll g_gamepad;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_pRTV = nullptr;

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRTV);
    pBackBuffer->Release();
}

static void CleanupRenderTarget() {
    if (g_pRTV) {
        g_pRTV->Release();
        g_pRTV = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && g_pSwapChain) {
                g_pd3dContext->OMSetRenderTargets(0, nullptr, nullptr);
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                            DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"XeLauncher";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"XeLauncher",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900,
                              nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevels, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dContext);
    if (hr != S_OK) {
        MessageBoxW(nullptr, L"Failed to create D3D11 device.", L"XeLauncher",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    CreateRenderTarget();
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = _strdup((editorDataDir() / "imgui.ini").string().c_str());

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 12.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.878f, 0.878f, 0.878f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.502f, 0.502f, 0.502f, 1.f);
    c[ImGuiCol_WindowBg] = ImVec4(0.114f, 0.114f, 0.114f, 1.f);
    c[ImGuiCol_ChildBg] = ImVec4(0.141f, 0.141f, 0.141f, 1.f);
    c[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.09f, 0.09f, 0.09f, 1.f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.180f, 0.180f, 0.180f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.220f, 0.220f, 0.220f, 1.f);
    c[ImGuiCol_TitleBg] = ImVec4(0.114f, 0.114f, 0.114f, 1.f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.114f, 0.114f, 0.114f, 1.f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.114f, 0.114f, 0.114f, 1.f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.114f, 0.114f, 0.114f, 1.f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.220f, 0.220f, 0.220f, 1.f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.012f, 0.455f, 0.784f, 1.f);
    c[ImGuiCol_CheckMark] = ImVec4(0.118f, 0.565f, 0.882f, 1.f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.008f, 0.310f, 0.537f, 1.f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.012f, 0.455f, 0.784f, 1.f);
    c[ImGuiCol_Button] = ImVec4(0.180f, 0.180f, 0.180f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.220f, 0.220f, 0.220f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.008f, 0.310f, 0.537f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.180f, 0.180f, 0.180f, 1.f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.220f, 0.220f, 0.220f, 1.f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.008f, 0.310f, 0.537f, 1.f);
    c[ImGuiCol_Separator] = ImVec4(0.08f, 0.08f, 0.08f, 1.f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.008f, 0.310f, 0.537f, 1.f);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.012f, 0.455f, 0.784f, 1.f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.180f, 0.180f, 0.180f, 1.f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.008f, 0.310f, 0.537f, 1.f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.012f, 0.455f, 0.784f, 1.f);
    c[ImGuiCol_Tab] = ImVec4(0.114f, 0.114f, 0.114f, 1.f);
    c[ImGuiCol_TabHovered] = ImVec4(0.220f, 0.220f, 0.220f, 1.f);
    c[ImGuiCol_TabActive] = ImVec4(0.180f, 0.180f, 0.180f, 1.f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.114f, 0.114f, 0.114f, 1.f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.141f, 0.141f, 0.141f, 1.f);
    c[ImGuiCol_NavHighlight] = ImVec4(0.012f, 0.455f, 0.784f, 1.f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.180f, 0.180f, 0.180f, 1.f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.09f, 0.09f, 0.09f, 1.f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.09f, 0.09f, 0.09f, 1.f);

    static const ImWchar iconRanges[] = {0x2000, 0x2BFF, 0};
    ImFontConfig fontCfg;
    fontCfg.MergeMode = false;
    ImFont* uiFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f,
                                                  &fontCfg, io.Fonts->GetGlyphRangesDefault());
    fontCfg.MergeMode = true;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 16.0f, &fontCfg, iconRanges);
    if (!uiFont) io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

    g_gamepad.init(hInstance, hwnd);

    g_app.device = g_pd3dDevice;
    g_app.tex.device = g_pd3dDevice;
    g_app.hwnd = hwnd;
    g_app.init();

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        {
            // The Win32 backend sets HasGamepad when an XInput pad is present;
            // otherwise fall back to the winmm joystick poller for PS pads.
            ImGuiIO& io = ImGui::GetIO();
            const bool xinputActive = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
            g_gamepad.newFrame(xinputActive, g_app.runningProc == nullptr);
            if (xinputActive)
                io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            else
                io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) |
                                  (g_gamepad.connected() ? ImGuiBackendFlags_HasGamepad : 0);
        }
        ImGui::NewFrame();

        g_app.draw();

        ImGui::Render();
        const float clearColor[4] = {0.12f, 0.12f, 0.14f, 1.0f};
        g_pd3dContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
        g_pd3dContext->ClearRenderTargetView(g_pRTV, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    g_gamepad.shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget();
    g_app.shutdown();
    g_pSwapChain->Release();
    g_pd3dDevice->Release();
    g_pd3dContext->Release();
    CoUninitialize();

    return 0;
}
