#include "theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdlib>
#include <vector>

static ImVec4 accentColorVec(bool light) {
    return light ? ImVec4(0.102f, 0.498f, 0.216f, 1.0f)
                 : ImVec4(0.247f, 0.725f, 0.314f, 1.0f);
}

ImU32 accentColorU32(bool light) {
    return ImGui::ColorConvertFloat4ToU32(accentColorVec(light));
}

static bool g_themeLight = false;

bool themeIsLight() { return g_themeLight; }

void applyAppTheme(bool light) {
    g_themeLight = light;
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 0.0f;
    style.ChildRounding = 10.0f;
    style.PopupRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.TabRounding = 7.0f;
    style.GrabRounding = 7.0f;
    style.ScrollbarRounding = 8.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 12.0f;

    ImVec4 accent = accentColorVec(light);
    ImVec4 accentDim = light ? ImVec4(0.16f, 0.42f, 0.24f, 1.0f)
                             : ImVec4(0.13f, 0.35f, 0.19f, 1.0f);
    ImVec4* c = style.Colors;

    if (light) {
        c[ImGuiCol_Text] = ImVec4(0.118f, 0.141f, 0.173f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.451f, 0.494f, 0.553f, 1.0f);
        c[ImGuiCol_WindowBg] = ImVec4(0.941f, 0.953f, 0.969f, 0.90f);
        c[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.45f);
        c[ImGuiCol_PopupBg] = ImVec4(0.984f, 0.988f, 0.996f, 0.985f);
        c[ImGuiCol_Border] = ImVec4(0.10f, 0.12f, 0.15f, 0.14f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.925f, 0.941f, 0.957f, 0.85f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.882f, 0.906f, 0.933f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.839f, 0.867f, 0.902f, 1.0f);
        c[ImGuiCol_TitleBg] = ImVec4(0.941f, 0.953f, 0.969f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.941f, 0.953f, 0.969f, 1.0f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.941f, 0.953f, 0.969f, 1.0f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.957f, 0.965f, 0.980f, 0.82f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.941f, 0.953f, 0.969f, 0.55f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.72f, 0.75f, 0.79f, 0.85f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.64f, 0.69f, 1.0f);
        c[ImGuiCol_ScrollbarGrabActive] = accent;
        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accentDim;
        c[ImGuiCol_SliderGrabActive] = accent;
        c[ImGuiCol_Button] = ImVec4(0.918f, 0.933f, 0.953f, 0.92f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.859f, 0.886f, 0.922f, 1.0f);
        c[ImGuiCol_ButtonActive] = accentDim;
        c[ImGuiCol_Header] = ImVec4(0.906f, 0.925f, 0.949f, 0.80f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.71f, 0.84f, 0.75f, 0.85f);
        c[ImGuiCol_HeaderActive] = accent;
        c[ImGuiCol_Separator] = ImVec4(0.10f, 0.12f, 0.15f, 0.12f);
        c[ImGuiCol_SeparatorHovered] = accentDim;
        c[ImGuiCol_SeparatorActive] = accent;
        c[ImGuiCol_ResizeGrip] = ImVec4(0.10f, 0.12f, 0.15f, 0.10f);
        c[ImGuiCol_ResizeGripHovered] = accentDim;
        c[ImGuiCol_ResizeGripActive] = accent;
        c[ImGuiCol_Tab] = ImVec4(0.93f, 0.94f, 0.96f, 0.45f);
        c[ImGuiCol_TabHovered] = ImVec4(0.82f, 0.87f, 0.91f, 1.0f);
        c[ImGuiCol_TabSelected] = ImVec4(0.97f, 0.98f, 0.99f, 1.0f);
        c[ImGuiCol_TabDimmed] = ImVec4(0.93f, 0.94f, 0.96f, 0.45f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.95f, 0.96f, 0.975f, 1.0f);
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.894f, 0.914f, 0.937f, 0.92f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.10f, 0.12f, 0.15f, 0.14f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.10f, 0.12f, 0.15f, 0.08f);
        c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(0.0f, 0.0f, 0.0f, 0.025f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_PlotLines] = ImVec4(0.35f, 0.40f, 0.47f, 1.0f);
        c[ImGuiCol_PlotHistogram] = accent;
    } else {
        c[ImGuiCol_Text] = ImVec4(0.878f, 0.902f, 0.929f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.520f, 0.561f, 0.620f, 1.0f);
        c[ImGuiCol_WindowBg] = ImVec4(0.043f, 0.055f, 0.075f, 0.88f);
        c[ImGuiCol_ChildBg] = ImVec4(0.078f, 0.098f, 0.125f, 0.44f);
        c[ImGuiCol_PopupBg] = ImVec4(0.067f, 0.086f, 0.114f, 0.975f);
        c[ImGuiCol_Border] = ImVec4(0.95f, 0.98f, 1.0f, 0.09f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.110f, 0.145f, 0.184f, 0.72f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.153f, 0.200f, 0.251f, 0.85f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.184f, 0.239f, 0.298f, 0.95f);
        c[ImGuiCol_TitleBg] = ImVec4(0.043f, 0.055f, 0.075f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.043f, 0.055f, 0.075f, 1.0f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.043f, 0.055f, 0.075f, 1.0f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.055f, 0.071f, 0.094f, 0.78f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.043f, 0.055f, 0.075f, 0.50f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.220f, 0.267f, 0.325f, 0.80f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.310f, 0.365f, 0.435f, 1.0f);
        c[ImGuiCol_ScrollbarGrabActive] = accent;
        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accentDim;
        c[ImGuiCol_SliderGrabActive] = accent;
        c[ImGuiCol_Button] = ImVec4(0.133f, 0.173f, 0.216f, 0.78f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.192f, 0.247f, 0.306f, 0.92f);
        c[ImGuiCol_ButtonActive] = accentDim;
        c[ImGuiCol_Header] = ImVec4(0.133f, 0.176f, 0.220f, 0.72f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.28f, 0.21f, 0.85f);
        c[ImGuiCol_HeaderActive] = accent;
        c[ImGuiCol_Separator] = ImVec4(0.95f, 1.0f, 1.0f, 0.07f);
        c[ImGuiCol_SeparatorHovered] = accentDim;
        c[ImGuiCol_SeparatorActive] = accent;
        c[ImGuiCol_ResizeGrip] = ImVec4(0.95f, 1.0f, 1.0f, 0.06f);
        c[ImGuiCol_ResizeGripHovered] = accentDim;
        c[ImGuiCol_ResizeGripActive] = accent;
        c[ImGuiCol_Tab] = ImVec4(0.075f, 0.094f, 0.120f, 0.42f);
        c[ImGuiCol_TabHovered] = ImVec4(0.180f, 0.235f, 0.290f, 0.90f);
        c[ImGuiCol_TabSelected] = ImVec4(0.125f, 0.200f, 0.165f, 0.95f);
        c[ImGuiCol_TabDimmed] = ImVec4(0.075f, 0.094f, 0.120f, 0.42f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.100f, 0.140f, 0.150f, 0.90f);
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.098f, 0.129f, 0.161f, 0.92f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.95f, 1.0f, 1.0f, 0.10f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.95f, 1.0f, 1.0f, 0.05f);
        c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.022f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.32f);
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_PlotLines] = ImVec4(0.52f, 0.60f, 0.70f, 1.0f);
        c[ImGuiCol_PlotHistogram] = accent;
    }

    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
    c[ImGuiCol_ModalWindowDimBg] = light ? ImVec4(0.35f, 0.40f, 0.46f, 0.38f)
                                         : ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

struct FxParticle {
    float baseX, y, vy, size, phase, swayAmp, swaySpd, alpha;
    int glyph;
    bool accent;
};

static std::vector<FxParticle> g_particles;

static const char* const kFxGlyphs[] = {
    u8"\u24CD", u8"\u24B6", u8"\u24B7", u8"\u24CE",
    u8"\u2605", u8"\u25CF", u8"\u25C6", u8"\u25B2",
};

static const int kFxWeights[] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7};

static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)rand() / (float)RAND_MAX;
}

static ImU32 colWithAlpha(const ImVec4& c, float a) {
    ImVec4 t = c;
    t.w = a;
    return ImGui::ColorConvertFloat4ToU32(t);
}

static void softBlob(ImDrawList* dl, ImVec2 center, float radius, const ImVec4& color,
                     float centerAlpha, int rings) {
    for (int i = rings; i >= 1; i--) {
        float f = (float)i / (float)rings;
        float a = centerAlpha * (3.0f / (float)rings) * (1.0f - f) * (1.0f - f);
        if (a < 0.6f) continue;
        dl->AddCircleFilled(center, radius * f, colWithAlpha(color, a), 48);
    }
}

static ImVec4 lerp4(const ImVec4& a, const ImVec4& b, float f) {
    return ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f,
                  a.z + (b.z - a.z) * f, 1.0f);
}

void drawBackgroundFx(bool light) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 sz = ImGui::GetMainViewport()->WorkSize;
    if (sz.x <= 1.0f || sz.y <= 1.0f) return;
    const float t = (float)ImGui::GetTime();

    ImVec4 tlA, trA, brA, blA, tlB, trB, brB, blB;
    if (light) {
        tlA = ImVec4(0.933f, 0.953f, 0.980f, 1.f); trA = ImVec4(0.906f, 0.933f, 0.969f, 1.f);
        brA = ImVec4(0.914f, 0.949f, 0.925f, 1.f); blA = ImVec4(0.918f, 0.941f, 0.973f, 1.f);
        tlB = ImVec4(0.914f, 0.937f, 0.973f, 1.f); trB = ImVec4(0.929f, 0.953f, 0.933f, 1.f);
        brB = ImVec4(0.906f, 0.933f, 0.973f, 1.f); blB = ImVec4(0.925f, 0.949f, 0.918f, 1.f);
    } else {
        tlA = ImVec4(0.063f, 0.106f, 0.173f, 1.f); trA = ImVec4(0.047f, 0.082f, 0.149f, 1.f);
        brA = ImVec4(0.078f, 0.141f, 0.227f, 1.f); blA = ImVec4(0.051f, 0.102f, 0.078f, 1.f);
        tlB = ImVec4(0.051f, 0.082f, 0.141f, 1.f); trB = ImVec4(0.071f, 0.137f, 0.227f, 1.f);
        brB = ImVec4(0.055f, 0.125f, 0.094f, 1.f); blB = ImVec4(0.063f, 0.110f, 0.188f, 1.f);
    }
    float sTL = 0.5f + 0.5f * sinf(t * 0.05f);
    float sTR = 0.5f + 0.5f * sinf(t * 0.05f + 1.3f);
    float sBR = 0.5f + 0.5f * sinf(t * 0.05f + 2.6f);
    float sBL = 0.5f + 0.5f * sinf(t * 0.05f + 4.2f);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), sz,
                                ImGui::ColorConvertFloat4ToU32(lerp4(tlA, tlB, sTL)),
                                ImGui::ColorConvertFloat4ToU32(lerp4(trA, trB, sTR)),
                                ImGui::ColorConvertFloat4ToU32(lerp4(brA, brB, sBR)),
                                ImGui::ColorConvertFloat4ToU32(lerp4(blA, blB, sBL)));

    softBlob(dl, ImVec2(sz.x * 0.85f, sz.y * 0.10f), sz.y * 0.50f, accentColorVec(light),
             light ? 20.0f : 15.0f, 30);
    softBlob(dl, ImVec2(sz.x * 0.08f, sz.y * 0.92f), sz.y * 0.45f,
             ImVec4(0.22f, 0.52f, 1.0f, 1.f), light ? 16.0f : 11.0f, 30);

    if (g_particles.empty()) {
        srand(1337);
        for (int i = 0; i < 46; i++) {
            FxParticle p;
            p.baseX = frand(-40.0f, sz.x + 40.0f);
            p.y = frand(-sz.y, sz.y);
            p.size = frand(13.0f, 38.0f);
            p.vy = frand(14.0f, 44.0f) * (0.6f + p.size / 40.0f);
            p.phase = frand(0.0f, 6.2831f);
            p.swayAmp = frand(10.0f, 38.0f);
            p.swaySpd = frand(0.25f, 0.85f);
            p.alpha = frand(0.35f, 1.0f);
            p.glyph = kFxWeights[rand() % 14];
            p.accent = (rand() % 100) < 42;
            g_particles.push_back(p);
        }
    }

    const float dt = ImGui::GetIO().DeltaTime;
    ImVec4 neutral = light ? ImVec4(0.27f, 0.31f, 0.37f, 1.f)
                           : ImVec4(0.80f, 0.84f, 0.90f, 1.f);
    for (auto& p : g_particles) {
        p.y += p.vy * dt;
        if (p.y > sz.y + p.size) {
            p.y = -p.size - frand(0.0f, 120.0f);
            p.baseX = frand(-40.0f, sz.x + 40.0f);
            p.size = frand(13.0f, 38.0f);
            p.vy = frand(14.0f, 44.0f) * (0.6f + p.size / 40.0f);
        }
        float x = p.baseX + sinf(t * p.swaySpd + p.phase) * p.swayAmp;
        float edge = 1.0f;
        if (p.y < 60.0f) edge = p.y / 60.0f;
        if (sz.y - p.y < 60.0f) edge = (sz.y - p.y) / 60.0f;
        if (edge <= 0.0f || x < -p.size || x > sz.x + p.size) continue;
        float a = p.alpha * edge * (light ? 0.34f : 0.28f);
        ImVec4 col = p.accent ? accentColorVec(light) : neutral;
        dl->AddText(ImGui::GetFont(), p.size, ImVec2(x, p.y),
                    colWithAlpha(col, a), kFxGlyphs[p.glyph]);
    }
}

bool gradientButton(const char* label, const ImVec2& size_arg, bool primary) {
    ImVec2 labelSz = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 pad = ImGui::GetStyle().FramePadding;
    ImVec2 size = size_arg;
    if (size.x <= 0.0f) size.x = labelSz.x + pad.x * 2.0f;
    if (size.y <= 0.0f) size.y = labelSz.y + pad.y * 2.0f;

    ImVec4 top, bot;
    if (primary) {
        top = ImVec4(0.196f, 0.604f, 0.325f, 1.0f);
        bot = ImVec4(0.078f, 0.400f, 0.180f, 1.0f);
    } else if (ImGui::GetStyle().Colors[ImGuiCol_Text].x < 0.5f) {
        top = ImVec4(0.949f, 0.961f, 0.976f, 1.0f);
        bot = ImVec4(0.867f, 0.894f, 0.925f, 1.0f);
    } else {
        top = ImVec4(0.208f, 0.263f, 0.325f, 1.0f);
        bot = ImVec4(0.114f, 0.153f, 0.196f, 1.0f);
    }

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::PushID(label);
    bool pressed = ImGui::InvisibleButton("##gb", size);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImGui::PopID();
    if (hovered) {
        top.x += 0.05f; top.y += 0.05f; top.z += 0.04f;
        bot.x += 0.05f; bot.y += 0.05f; bot.z += 0.04f;
    }
    if (held) {
        top.x += 0.06f; top.y += 0.06f; top.z += 0.05f;
        bot.x += 0.06f; bot.y += 0.06f; bot.z += 0.05f;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = ImGui::GetStyle().FrameRounding;
    ImU32 border = ImGui::GetStyle().Colors[ImGuiCol_Text].x < 0.5f
                       ? IM_COL32(20, 30, 24, 60)
                       : IM_COL32(255, 255, 255, 40);
    ImU32 topU32 = ImGui::GetColorU32(top);
    ImU32 botU32 = ImGui::GetColorU32(bot);
    int vs = dl->VtxBuffer.Size;
    dl->AddRectFilled(p0, p1, topU32, rounding);
    int ve = dl->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, vs, ve, p0, ImVec2(p0.x, p1.y),
                                                  topU32, botU32);
    dl->AddRect(p0, p1, border, rounding, 0, 1.0f);

    ImU32 textCol = primary ? ImGui::GetColorU32(ImVec4(1, 1, 1, 1))
                            : ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImVec2(p0.x + (size.x - labelSz.x) * 0.5f,
                       p0.y + (size.y - labelSz.y) * 0.5f),
                textCol, label);
    return pressed;
}
