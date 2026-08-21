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
static bool g_lightTheme = false;
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

// Applies the ImGui palette (dark or light), the shared style tweaks, and the
// swapchain clear color. Called once at startup and again whenever the user
// toggles the theme from the View menu. Implemented in theme.cpp.

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
    applyAppTheme(g_app.lightTheme);  // settings (incl. theme) loaded by init()

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
        float clearColor[4];
        if (g_lightTheme) {
            clearColor[0] = 0.941f; clearColor[1] = 0.953f; clearColor[2] = 0.969f; clearColor[3] = 1.0f;
        } else {
            clearColor[0] = 0.043f; clearColor[1] = 0.055f; clearColor[2] = 0.075f; clearColor[3] = 1.0f;
        }
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
