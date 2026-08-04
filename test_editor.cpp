#include "editor.h"

#include <cstdio>
#include <filesystem>

int main() {
    std::filesystem::copy_file("sample.toml", "test_tmp.toml",
                               std::filesystem::copy_options::overwrite_existing);
    EditorApp app;
    app.xeniaDir = "C:\\xenia_canary_windows";
    app.defaultsPath = "defaults.toml";
    app.init();
    printf("settings=%zu categories=%zu\n", app.settings.size(), app.categories.size());

    int mod = 0, fav = 0;
    for (auto& s : app.settings) {
        if (s.modified) mod++;
        if (s.favorite) fav++;
    }
    printf("modified=%d favorites=%d\n", mod, fav);
    for (auto& s : app.settings)
        if (s.modified) printf("  modified: %s default=%s current=%s\n", s.full.c_str(),
                               s.defaultRaw.c_str(), s.effRaw.c_str());

    app.target.load("test_tmp.toml");
    app.global.load("test_tmp.toml");
    app.rebuild();

    SettingInfo* fs = nullptr;
    for (auto& s : app.settings)
        if (s.full == "Display.fullscreen") fs = &s;
    if (!fs) {
        printf("fullscreen not found\n");
        return 1;
    }
    printf("fullscreen type=%d isEnum=%d isPath=%d unsafe=%d present=%d\n", (int)fs->type,
           fs->isEnum, fs->isPath, fs->unsafe, fs->present);
    app.toggleFavorite("Display.fullscreen");
    app.applyEdit(*fs, "false");
    printf("after edit: eff=%s modified=%d dirty=%d\n", fs->effRaw.c_str(), fs->modified,
           app.target.dirty());
    app.saveTarget();
    printf("after save dirty=%d\n", app.target.dirty());

    ConfigFile check;
    check.load("test_tmp.toml");
    ConfigEntry* e = check.find("Display", "fullscreen");
    printf("on-disk fullscreen=%s\n", e ? e->raw.c_str() : "MISSING");

    std::string favFile = std::filesystem::exists("favorites.txt") ? "yes" : "no";
    printf("favorites.txt written: %s\n", favFile.c_str());

    std::filesystem::remove("test_tmp.toml");
    std::filesystem::remove("favorites.txt");
    std::filesystem::remove("editor.settings.txt");
    return 0;
}
