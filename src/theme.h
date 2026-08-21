#pragma once

#include <imgui.h>

// Applies the modern ImGui palette (dark or light) plus shared style tweaks.
void applyAppTheme(bool light);

// Animated background: slow-shifting corner gradient, soft glow blobs and
// drifting controller-button glyphs. Call once per frame before any windows.
void drawBackgroundFx(bool light);

// Button rendered with a vertical color gradient. primary = green accent,
// otherwise neutral slate. Honors BeginDisabled.
bool gradientButton(const char* label, const ImVec2& size, bool primary);

ImU32 accentColorU32(bool light);
ImVec4 accentColorVec(bool light);
bool themeIsLight();
