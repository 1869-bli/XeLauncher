#include "editor.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

int main() {
    std::filesystem::create_directories("test_pg");
    std::filesystem::copy_file("sample.toml", "test_pg/global.toml",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("sample.toml", "test_pg/ABCDEF01.config.toml",
                               std::filesystem::copy_options::overwrite_existing);

    EditorApp app;
    app.xeniaDir = "test_pg";
    app.defaultsPath = "defaults.toml";
    app.init();
    app.global.load("test_pg/global.toml");
    app.target.load("test_pg/ABCDEF01.config.toml");
    app.editingPerGame = true;
    app.rebuild();

    SettingInfo* fs = nullptr;
    int present = 0;
    for (auto& s : app.settings) {
        if (s.present) present++;
        if (s.full == "Display.fullscreen") fs = &s;
    }
    printf("per-game present count=%d (should be 263, file is full config)\n", present);
    if (fs) {
        printf("fullscreen base=%s eff=%s modified=%d\n", fs->baseRaw.c_str(), fs->effRaw.c_str(),
               fs->modified);
    }

    std::filesystem::path pg = "test_pg/mini.config.toml";
    std::string mini = "# mini\n";
    {
        std::ofstream ofs(pg, std::ios::trunc);
        ofs << mini;
    }
    app.target.load(pg);
    app.rebuild();
    int present2 = 0;
    for (auto& s : app.settings)
        if (s.present) present2++;
    printf("mini config present count=%d (should be 0)\n", present2);

    SettingInfo* fsm = nullptr;
    for (auto& s : app.settings)
        if (s.full == "Display.fullscreen") fsm = &s;
    if (fsm) {
        printf("mini fullscreen base=%s eff=%s modified=%d present=%d\n", fsm->baseRaw.c_str(),
               fsm->effRaw.c_str(), fsm->modified, fsm->present);
        app.applyEdit(*fsm, "false");
        printf("after override: present=%d modified=%d dirty=%d\n", fsm->present, fsm->modified,
               app.target.dirty());
        app.saveTarget();
    }
    std::string contents;
    {
        std::ifstream in(pg, std::ios::binary);
        contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    printf("--- mini config contents ---\n%s\n", contents.c_str());
    std::filesystem::remove_all("test_pg");
    return 0;
}
