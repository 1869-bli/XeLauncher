#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct GameEntry {
    std::string name;
    std::string titleId;  // 8 uppercase hex chars, may be empty
    std::string path;     // native path to iso/xex/folder
    std::string type;     // "xex" | "iso" | "folder" | "file"
    std::string cover;    // local cover image path, may be empty
    std::string launchArgs;  // extra args appended to the xenia launch command
    long long lastPlayed = 0;
    int launches = 0;
    long long playtimeSec = 0;
    bool fav = false;
};

struct Library {
    std::filesystem::path dataDir;
    std::vector<GameEntry> games;

    bool load();
    bool save();
    GameEntry* find(const std::string& path);
    int add(const GameEntry& e);
    bool remove(int idx);
};

std::string detectTitleIdFromFile(const std::filesystem::path& p);
std::string guessGameName(const std::string& path);

// Finds a "Box Front" cover URL for a game title from the LaunchBox metadata
// (fetched lazily and cached in dataDir). Returns false on any failure.
bool lookupCoverUrl(const std::filesystem::path& dataDir, const std::string& name,
                    std::string& urlOut);

// Downloads the LaunchBox metadata into dataDir/launchbox.json if missing.
// Meant for a background thread (blocks until done).
bool fetchLaunchBoxCache(const std::filesystem::path& dataDir);
