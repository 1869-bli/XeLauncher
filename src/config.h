#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

enum class ValType { Bool, Int, Float, Str, Arr, Raw };

struct ConfigEntry {
    std::string category;
    std::string key;
    ValType type = ValType::Raw;
    std::string raw;
    std::vector<std::string> desc;
    int line = -1;
    int endLine = -1;
    int valueStart = 0;
    int valueEnd = 0;
    bool dirty = false;
    bool deleted = false;
    bool added = false;
    std::string full() const { return category + "." + key; }
};

struct ConfigSection {
    std::string name;
    std::vector<ConfigEntry*> entries;
};

struct ConfigFile {
    std::filesystem::path path;
    std::vector<std::string> lines;
    std::vector<ConfigSection> sections;
    std::vector<std::unique_ptr<ConfigEntry>> entries;
    bool loaded = false;
    bool crlf = true;
    std::string error;

    bool load(const std::filesystem::path& p);
    bool save();
    bool dirty() const;
    ConfigEntry* find(const std::string& category, const std::string& key);
    ConfigEntry* set(const std::string& category, const std::string& key,
                     ValType type, const std::string& raw,
                     const std::vector<std::string>& desc);
    bool erase(const std::string& category, const std::string& key);
    std::vector<std::string> categories() const;
};

std::string decodeStr(const std::string& raw);
std::string encodeStr(const std::string& v);
std::string normalizeInt(const std::string& v);
std::string normalizeFloat(const std::string& v);
bool looksLikeFloat(const std::string& raw);
bool looksLikeBool(const std::string& raw);
bool looksLikeInt(const std::string& raw);
std::string trimStr(const std::string& s);
