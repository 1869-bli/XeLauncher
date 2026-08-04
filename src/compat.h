#pragma once
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct CompatEntry {
    std::string id;
    std::string title;
    std::string state;
    std::string updated;
    std::string url;
};

struct CompatDB {
    std::map<std::string, CompatEntry> byId;
    std::filesystem::path cachePath;
    bool loaded = false;
    std::string source;   // "cached" | "online" | ""
    std::string fetchedAt;
    std::string lastError;
    std::atomic<bool> busy{false};
    std::thread thread;
    mutable std::mutex mtx;

    void startFetch();
    void fetchAndSave();
    bool loadCache();
    void saveCache();
    void shutdown();
    // Thread-safe accessors (byId is rebuilt by the fetch worker thread).
    CompatEntry find(const std::string& titleId) const;
    size_t size() const;
    std::vector<CompatEntry> all() const;
};
