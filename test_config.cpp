#include "config.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: test_config <file>\n");
        return 1;
    }
    ConfigFile cf;
    if (!cf.load(argv[1])) {
        printf("load failed: %s\n", cf.error.c_str());
        return 1;
    }
    printf("entries=%zu sections=%zu\n", cf.entries.size(), cf.sections.size());
    ConfigEntry* fullscreen = cf.find("Display", "fullscreen");
    if (fullscreen) {
        printf("fullscreen type=%d raw=%s descLines=%zu\n", (int)fullscreen->type,
               fullscreen->raw.c_str(), fullscreen->desc.size());
        cf.set("Display", "fullscreen", fullscreen->type, "false", {});
    }
    ConfigEntry* pvr = cf.find("CPU", "pvr");
    if (pvr) printf("pvr raw=%s descLines=%zu firstDesc=[%s]\n", pvr->raw.c_str(),
                    pvr->desc.size(), pvr->desc.empty() ? "" : pvr->desc[0].c_str());
    ConfigEntry* xma = cf.find("APU", "xma_decoder");
    if (xma) printf("xma raw=%s descLines=%zu\n", xma->raw.c_str(), xma->desc.size());
    ConfigEntry* str = cf.find("Profiles", "logged_profile_slot_0_xuid");
    if (str) printf("xuid raw=%s\n", str->raw.c_str());
    ConfigEntry* m = cf.find("Memory", "allow_invalid_memory_access");
    if (!m) {
        ConfigEntry* ne = cf.set("Memory", "allow_invalid_memory_access", ValType::Bool,
                                 "false", {"Allow invalid memory access", "Use: [true, false]"});
        printf("added key, present=%d\n", ne ? 1 : 0);
    }
    if (!cf.save()) {
        printf("save failed\n");
        return 1;
    }
    ConfigFile cf2;
    if (!cf2.load(argv[1])) {
        printf("reload failed\n");
        return 1;
    }
    ConfigEntry* fs2 = cf2.find("Display", "fullscreen");
    printf("reload fullscreen=%s\n", fs2 ? fs2->raw.c_str() : "MISSING");
    ConfigEntry* m2 = cf2.find("Memory", "allow_invalid_memory_access");
    printf("reload added key=%s (ptr=%p)\n", m2 ? m2->raw.c_str() : "MISSING", (void*)m2);
    int match = 0;
    for (auto& e : cf2.entries)
        if (e->category == "Memory" && e->key == "allow_invalid_memory_access") match++;
    printf("manual find matches=%d\n", match);
    for (auto& e : cf2.entries)
        if (e->key.find("allow_invalid") != std::string::npos)
            printf("  found [%s] %s = %s (cat len=%zu)\n", e->category.c_str(), e->key.c_str(), e->raw.c_str(), e->category.size());
    printf("total after reload=%zu\n", cf2.entries.size());
    return 0;
}
