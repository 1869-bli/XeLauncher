#include "editor.h"

#include "net.h"

#define NOMINMAX
#include <windows.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <thread>

static const char* const ICON_FAV = u8"\u2605";
static const char* const ICON_FAV_OFF = u8"\u2606";
static const char* const ICON_DOT = u8"\u25CF";
static const char* const ICON_RESET = u8"\u21BA";
static const char* const ICON_WARN = u8"\u26A0";

static std::filesystem::path g_exeDir;

static std::filesystem::path exeDir() {
    if (!g_exeDir.empty()) return g_exeDir;
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    g_exeDir = std::filesystem::path(buf).parent_path();
    return g_exeDir;
}

std::filesystem::path editorDataDir() {
    wchar_t buf[MAX_PATH];
    std::wstring appData;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) appData = buf;
    auto p = std::filesystem::path(appData) / "XeLauncher";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

// One-time move of data from the pre-rename %LOCALAPPDATA%\XeniaConfigEditor
// folder so existing libraries, covers and caches are preserved.
static void migrateLegacyDataDir() {
    wchar_t buf[MAX_PATH];
    std::wstring appData;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) appData = buf;
    if (appData.empty()) return;
    auto legacy = std::filesystem::path(appData) / "XeniaConfigEditor";
    auto cur = editorDataDir();
    std::error_code ec;
    if (!std::filesystem::exists(legacy, ec) || ec) return;
    if (std::filesystem::exists(cur / "library.json", ec)) return;  // already migrated
    std::filesystem::copy(legacy, cur,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
}

static std::wstring utf8ToWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, 0);
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

static std::string wideToUtf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::string fileToString(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static bool stringToFile(const std::filesystem::path& p, const std::string& s) {
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(s.data(), (std::streamsize)s.size());
    return (bool)ofs;
}

static bool pickDialog(const wchar_t* title, bool folder, std::filesystem::path& out) {
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dlg));
    if (FAILED(hr)) return false;
    dlg->SetTitle(title);
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (folder) opts |= FOS_PICKFOLDERS;
    dlg->SetOptions(opts);
    hr = dlg->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dlg->GetResult(&item);
        if (SUCCEEDED(hr) && item) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                out = psz;
                CoTaskMemFree(psz);
            }
            item->Release();
        }
    }
    dlg->Release();
    return SUCCEEDED(hr);
}

static std::string truncateUtf8(const std::string& s, int maxBytes) {
    if ((int)s.size() <= maxBytes) return s;
    int cut = maxBytes;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    if (cut <= 0) return "…";
    return s.substr(0, cut) + "…";
}

static long long steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Manual library navigation (controller). The tab bar, toolbar filters, the
// game grid/list and the details sidebar form one flat sequence of targets, so
// a D-pad press always moves exactly one item (never a whole row) and every
// control stays reachable.
enum {
    kNavTabLibrary = 0, kNavTabConfig, kNavTabCompat,
    kNavAdd, kNavAddFolder, kNavScan, kNavImport, kNavView, kNavSearch,
    kNavClear, kNavSort, kNavStatus, kNavFav, kNavRefresh, kNavToolbar,
    kNavDetailLaunch, kNavDetailFav, kNavDetailShortcut, kNavDetailEdit,
    kNavDetailPerGame, kNavDetailCover, kNavDetailSetCover, kNavDetailFolder,
    kNavDetailRemove, kNavDetailLast,
};

static void drawNavCursorRect() {
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - 2.0f, min.y - 2.0f),
                                        ImVec2(max.x + 2.0f, max.y + 2.0f),
                                        IM_COL32(255, 182, 39, 255), 0.0f, 0, 2.0f);
}


static std::string formatPlaytime(long long sec) {
    if (sec < 60) return std::to_string(sec) + "s";
    long long m = sec / 60;
    if (m < 60) return std::to_string(m) + "m";
    long long h = m / 60;
    long long rm = m % 60;
    char b[32];
    if (h < 100)
        snprintf(b, sizeof(b), "%lldh %02lldm", h, rm);
    else
        snprintf(b, sizeof(b), "%lldh", h);
    return b;
}

static std::string formatDate(long long t) {
    if (t <= 0) return "never";
    std::time_t tt = (std::time_t)t;
    std::tm tm = {};
    localtime_s(&tm, &tt);
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%d", &tm);
    return b;
}

static bool containsIgnoreCase(const std::string& hay, const std::string& needle) {
    if (hay.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); i++) {
        size_t j = 0;
        for (; j < needle.size(); j++) {
            char a = hay[i + j], b = needle[j];
            if (toupper((unsigned char)a) != toupper((unsigned char)b)) break;
        }
        if (j == needle.size()) return true;
    }
    return false;
}

static std::string lowerStr(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::string sanitizeFileName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
            c == '|' || c == '?' || c == '*') {
            out += '_';
        } else if (c < 32) {
            out += '_';
        } else {
            out += (char)c;
        }
    }
    if (out.empty()) out = "game";
    return out;
}

static bool pickGameFiles(std::vector<std::filesystem::path>& out) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;
    dlg->SetTitle(L"Add game files");
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST;
    dlg->SetOptions(opts);
    COMDLG_FILTERSPEC filters[] = {
        {L"Xbox 360 games", L"*.iso;*.xex;*.zar;*.xbe;*.god"},
        {L"All files", L"*.*"},
    };
    dlg->SetFileTypes(2, filters);
    dlg->SetDefaultExtension(L"iso");
    if (FAILED(dlg->Show(nullptr))) return false;
    ComPtr<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items))) return false;
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; i++) {
        ComPtr<IShellItem> item;
        if (FAILED(items->GetItemAt(i, &item))) continue;
        PWSTR psz = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
            out.emplace_back(psz);
            CoTaskMemFree(psz);
        }
    }
    return !out.empty();
}

static bool pickOneGameFile(std::filesystem::path& out) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;
    dlg->SetTitle(L"Choose game file");
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
    dlg->SetOptions(opts);
    COMDLG_FILTERSPEC filters[] = {
        {L"Xbox 360 games", L"*.iso;*.xex;*.zar;*.xbe;*.god"},
        {L"All files", L"*.*"},
    };
    dlg->SetFileTypes(2, filters);
    if (FAILED(dlg->Show(nullptr))) return false;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return false;
    PWSTR psz = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) || !psz) return false;
    out = psz;
    CoTaskMemFree(psz);
    return true;
}

static bool pickImageFile(std::filesystem::path& out) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return false;
    dlg->SetTitle(L"Choose cover image");
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
    dlg->SetOptions(opts);
    COMDLG_FILTERSPEC filters[] = {
        {L"Images", L"*.png;*.jpg;*.jpeg;*.bmp"},
        {L"All files", L"*.*"},
    };
    dlg->SetFileTypes(2, filters);
    if (FAILED(dlg->Show(nullptr))) return false;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return false;
    PWSTR psz = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) || !psz) return false;
    out = psz;
    CoTaskMemFree(psz);
    return true;
}

static ImU32 compatColor(const std::string& state) {
    if (state == "Playable") return IM_COL32(70, 190, 90, 255);
    if (state == "Gameplay") return IM_COL32(235, 180, 45, 255);
    if (state == "Loads") return IM_COL32(85, 145, 225, 255);
    if (state == "Unplayable") return IM_COL32(220, 70, 60, 255);
    return IM_COL32(135, 135, 135, 255);
}

static bool parseEnumOptions(const std::vector<std::string>& desc, std::vector<EnumOption>& out) {
    size_t listIdx = std::string::npos;
    for (size_t i = 0; i < desc.size(); i++) {
        if (desc[i].find("Use:") != std::string::npos && desc[i].find('[') != std::string::npos) {
            listIdx = i;
            break;
        }
    }
    if (listIdx == std::string::npos) return false;
    std::string line = desc[listIdx];
    size_t ob = line.find('[');
    size_t cb = line.find(']');
    if (ob == std::string::npos || cb == std::string::npos || cb <= ob) return false;
    std::string inner = line.substr(ob + 1, cb - ob - 1);
    std::stringstream ss(inner);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok = trimStr(tok);
        if (tok.empty()) continue;
        EnumOption opt;
        opt.value = tok;
        opt.label = tok;
        for (size_t i = listIdx + 1; i < desc.size(); i++) {
            std::string d = trimStr(desc[i]);
            if (d.empty()) continue;
            if (d.find(tok + ":") == 0) {
                opt.desc = d.substr(tok.size() + 1);
                break;
            }
        }
        out.push_back(opt);
    }
    return !out.empty();
}

static bool descHasAny(const std::string& desc, const std::vector<const char*>& words) {
    for (auto* w : words)
        if (containsIgnoreCase(desc, w)) return true;
    return false;
}

static bool isPathSetting(const std::string& key, const std::string& desc) {
    static const std::vector<const char*> nameHints = {
        "path", "file", "folder", "directory", "filename", "_dir",
    };
    for (auto* h : nameHints)
        if (containsIgnoreCase(key, h)) return true;
    static const std::vector<const char*> descHints = {
        "path to", "file to", "folder", "directory", "loads a .map", "file to write",
    };
    return descHasAny(desc, descHints);
}

static bool pathIsFolderSetting(const std::string& key, const std::string& desc) {
    return containsIgnoreCase(key, "folder") || containsIgnoreCase(key, "directory") ||
           containsIgnoreCase(key, "_dir") || containsIgnoreCase(desc, "folder") ||
           containsIgnoreCase(desc, "directory");
}

static bool isUnsafeSetting(const std::string& key, const std::string& desc) {
    static const std::vector<const char*> keywords = {
        "not for users", "breaks games", "experimental", "for developer use",
        "not intended for actual debugging", "debugging", "debug",
        "may produce incorrect", "stress testing",
    };
    static const std::vector<const char*> nameHints = {
        "dump_", "trace_", "validate_", "debug_", "disassemble_", "break_on",
        "no_reserved", "permit_float", "writable_code", "full_optimization",
        "store_all_context", "emit_source_annotations", "record_mmio_access",
    };
    return descHasAny(desc, keywords) || descHasAny(key, nameHints);
}

static std::filesystem::path globalConfigPath(const std::filesystem::path& dir) {
    auto a = dir / "xenia-canary.config.toml";
    if (std::filesystem::exists(a)) return a;
    auto b = dir / "xenia.config.toml";
    if (std::filesystem::exists(b)) return b;
    return a;
}

static std::filesystem::path detectXeniaDir() {
    std::vector<std::filesystem::path> candidates;
    wchar_t buf[MAX_PATH];
    std::wstring appData;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) appData = buf;
    std::filesystem::path docs;
    {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p)) && p) {
            docs = p;
            CoTaskMemFree(p);
        }
    }
    candidates.push_back(L"C:\\xenia_canary_windows");
    candidates.push_back(L"C:\\xenia");
    candidates.push_back(L"C:\\xenia-canary");
    if (!appData.empty()) candidates.push_back(std::filesystem::path(appData) / "xenia");
    if (!docs.empty()) candidates.push_back(docs / "Xenia");
    for (auto& c : candidates) {
        if (std::filesystem::exists(c / "xenia_canary.exe") ||
            std::filesystem::exists(c / "xenia-canary.exe") ||
            std::filesystem::exists(c / "xenia-canary.config.toml") ||
            std::filesystem::exists(c / "xenia.config.toml")) {
            return c;
        }
    }
    return candidates[0];
}

static std::filesystem::path xeniaDirFromRegistry() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\XeLauncher", 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
        return {};
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(key, L"XeniaDir", nullptr, &type, (LPBYTE)buf, &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ || size == 0) return {};
    std::filesystem::path p(buf);
    return p;
}

static std::map<std::string, std::string> parseKeyValueLines(const std::string& s) {
    std::map<std::string, std::string> out;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[trimStr(line.substr(0, eq))] = trimStr(line.substr(eq + 1));
    }
    return out;
}

void EditorApp::loadAppSettings() {
    auto p = editorDataDir() / "editor.settings.txt";
    auto kv = parseKeyValueLines(fileToString(p));
    auto it = kv.find("xenia_dir");
    if (it != kv.end() && !it->second.empty()) xeniaDir = it->second;
    it = kv.find("defaults_file");
    if (it != kv.end() && !it->second.empty()) defaultsPath = it->second;
}

void EditorApp::saveAppSettings() {
    std::string s = "xenia_dir=" + xeniaDir.string() + "\n";
    s += "defaults_file=" + defaultsPath.string() + "\n";
    stringToFile(editorDataDir() / "editor.settings.txt", s);
}

void EditorApp::loadFavorites() {
    favorites.clear();
    std::stringstream ss(fileToString(editorDataDir() / "favorites.txt"));
    std::string line;
    while (std::getline(ss, line)) {
        line = trimStr(line);
        if (!line.empty()) favorites.insert(line);
    }
}

void EditorApp::saveFavorites() {
    std::string s;
    for (auto& f : favorites) s += f + "\n";
    stringToFile(editorDataDir() / "favorites.txt", s);
}

void EditorApp::setXeniaDir(const std::filesystem::path& p) {
    xeniaDir = p;
    saveAppSettings();
    auto gp = globalConfigPath(xeniaDir);
    global.load(gp);
    target.load(gp);
    scanPerGameFiles();
    selectedFile = 0;
    editingPerGame = false;
    rebuild();
}

void EditorApp::scanPerGameFiles() {
    openFiles.clear();
    if (xeniaDir.empty()) return;
    auto gp = globalConfigPath(xeniaDir);
    std::string gLabel = "Global config (" + gp.filename().string() + ")";
    openFiles.push_back({gLabel, gp});
    std::error_code ec;
    auto configDir = xeniaDir / "config";
    if (std::filesystem::exists(configDir)) {
        std::vector<FileEntry> games;
        for (auto& de : std::filesystem::directory_iterator(configDir, ec)) {
            if (!de.is_regular_file(ec)) continue;
            auto name = de.path().filename().string();
            if (name.size() > 12 && name.substr(name.size() - 12) == ".config.toml")
                games.push_back({name, de.path()});
        }
        std::sort(games.begin(), games.end(), [](const FileEntry& a, const FileEntry& b) {
            return a.label < b.label;
        });
        for (auto& g : games) openFiles.push_back(g);
    }
    openFiles.push_back({"Browse for config file...", {}});
}

void EditorApp::init() {
    migrateLegacyDataDir();
    loadAppSettings();
    loadFavorites();
    if (xeniaDir.empty() || !std::filesystem::exists(xeniaDir)) {
        auto reg = xeniaDirFromRegistry();
        if (!reg.empty() && std::filesystem::exists(reg)) xeniaDir = reg;
    }
    if (xeniaDir.empty() || !std::filesystem::exists(xeniaDir)) xeniaDir = detectXeniaDir();
    if (defaultsPath.empty() || !std::filesystem::exists(defaultsPath))
        defaultsPath = exeDir() / "defaults.toml";
    if (std::filesystem::exists(defaultsPath)) defaults.load(defaultsPath);
    if (!xeniaDir.empty()) {
        auto gp = globalConfigPath(xeniaDir);
        global.load(gp);
        target.load(gp);
    }
    scanPerGameFiles();
    selectedFile = 0;
    editingPerGame = false;
    rebuild();

    library.dataDir = editorDataDir();
    library.load();
    compat.cachePath = editorDataDir() / "compat_cache.json";
    compat.loadCache();
    compat.startFetch();
    ensureLaunchBoxCached();
}

void EditorApp::ensureLaunchBoxCached() {
    std::error_code ec;
    if (std::filesystem::exists(library.dataDir / "launchbox.json", ec)) return;
    if (launchboxFetching.exchange(true)) return;
    if (launchboxThread.joinable()) launchboxThread.join();
    launchboxThread = std::thread([this]() {
        fetchLaunchBoxCache(library.dataDir);
        launchboxFetching = false;
    });
}

void EditorApp::shutdown() {
    monitorRunning();
    if (launchboxThread.joinable()) launchboxThread.join();
    compat.shutdown();
    tex.releaseAll();
}

void EditorApp::refreshSettingValue(SettingInfo& s) {
    memset(s.buf, 0, sizeof(s.buf));
    if (s.type == ValType::Str) {
        std::string d = decodeStr(s.effRaw);
        snprintf(s.buf, sizeof(s.buf), "%s", d.c_str());
    } else {
        snprintf(s.buf, sizeof(s.buf), "%s", s.effRaw.c_str());
    }
    s.iVal = 0;
    s.fVal = 0.0;
    if (s.type == ValType::Int || s.type == ValType::Float) {
        try {
            if (s.type == ValType::Int)
                s.iVal = std::stoll(trimStr(s.effRaw), nullptr, 0);
            else
                s.fVal = std::stod(trimStr(s.effRaw));
        } catch (...) {
        }
    }
}

void EditorApp::rebuild() {
    settings.clear();
    categories.clear();
    settingsDirty = target.dirty();
    if (!defaults.loaded) return;
    for (auto& sec : defaults.sections) {
        categories.push_back(sec.name);
        for (auto* de : sec.entries) {
            SettingInfo s;
            s.category = de->category;
            s.key = de->key;
            s.full = de->full();
            s.type = de->type;
            s.description.clear();
            for (auto& d : de->desc) {
                if (!s.description.empty()) s.description += "\n";
                s.description += d;
            }
            s.isEnum = parseEnumOptions(de->desc, s.enums);
            s.isPath = isPathSetting(s.key, s.description);
            s.pathIsFolder = pathIsFolderSetting(s.key, s.description);
            s.unsafe = isUnsafeSetting(s.key, s.description);
            s.defaultRaw = de->raw;
            s.baseRaw = s.defaultRaw;
            ConfigEntry* ge = global.find(s.category, s.key);
            if (editingPerGame && ge && !ge->deleted) s.baseRaw = ge->raw;
            ConfigEntry* te = target.find(s.category, s.key);
            s.present = te && !te->deleted;
            if (s.present)
                s.type = te->type;
            else if (!editingPerGame && ge && !ge->deleted)
                s.type = ge->type;
            s.effRaw = s.present ? te->raw : s.baseRaw;
            if (s.type == ValType::Int && containsIgnoreCase(s.description, "0 to 1"))
                s.type = ValType::Float;
            s.modified = s.present ? (s.effRaw != s.defaultRaw) : false;
            if (editingPerGame) s.modified = s.present && s.effRaw != s.baseRaw;
            s.favorite = favorites.count(s.full) > 0;
            refreshSettingValue(s);
            settings.push_back(s);
        }
    }
}

void EditorApp::applyEdit(SettingInfo& s, const std::string& raw) {
    s.effRaw = raw;
    if (!s.present) {
        std::vector<std::string> desc;
        ConfigEntry* de = defaults.find(s.category, s.key);
        if (de) desc = de->desc;
        target.set(s.category, s.key, s.type, raw, desc);
        s.present = true;
    } else {
        target.set(s.category, s.key, s.type, raw, {});
    }
    refreshSettingValue(s);
    s.modified = editingPerGame ? (s.effRaw != s.baseRaw) : (s.effRaw != s.defaultRaw);
    settingsDirty = target.dirty();
}

void EditorApp::removeOverride(SettingInfo& s) {
    if (!s.present) return;
    target.erase(s.category, s.key);
    s.present = false;
    s.effRaw = s.baseRaw;
    s.modified = false;
    refreshSettingValue(s);
    settingsDirty = target.dirty();
}

void EditorApp::resetSetting(SettingInfo& s) {
    if (editingPerGame) {
        removeOverride(s);
    } else if (s.modified) {
        applyEdit(s, s.defaultRaw);
    }
}

void EditorApp::toggleFavorite(const std::string& full) {
    if (favorites.count(full))
        favorites.erase(full);
    else
        favorites.insert(full);
    saveFavorites();
    for (auto& s : settings)
        if (s.full == full) s.favorite = favorites.count(full) > 0;
}

void EditorApp::saveTarget() {
    if (target.save()) {
        statusMsg = "Saved: " + target.path.string();
        settingsDirty = false;
        rebuild();
    } else {
        statusMsg = "Save failed: " + target.path.string();
    }
}

void EditorApp::switchFile(int idx) {
    if (idx < 0 || idx >= (int)openFiles.size()) return;
    if (idx == (int)openFiles.size() - 1) {
        browseOpenFile();
        return;
    }
    selectedFile = idx;
    auto& fe = openFiles[idx];
    editingPerGame = idx > 0;
    if (target.load(fe.path)) {
        statusMsg = "Opened: " + fe.path.string();
    } else {
        statusMsg = "Failed to open: " + fe.path.string();
    }
    rebuild();
}

void EditorApp::browseOpenFile() {
    std::filesystem::path picked;
    if (!pickDialog(L"Open Xenia config file", false, picked)) return;
    for (size_t i = 0; i + 1 < openFiles.size(); i++) {
        if (openFiles[i].path == picked) {
            selectedFile = (int)i;
            editingPerGame = i > 0;
            target.load(picked);
            statusMsg = "Opened: " + picked.string();
            rebuild();
            return;
        }
    }
    FileEntry fe;
    fe.label = picked.filename().string() + " (external)";
    fe.path = picked;
    openFiles.insert(openFiles.end() - 1, fe);
    selectedFile = (int)openFiles.size() - 2;
    editingPerGame = false;
    target.load(picked);
    statusMsg = "Opened: " + picked.string();
    rebuild();
}

void EditorApp::createPerGame(const std::string& titleId) {
    if (titleId.empty() || xeniaDir.empty()) return;
    auto configDir = xeniaDir / "config";
    std::error_code ec;
    if (!std::filesystem::exists(configDir)) std::filesystem::create_directories(configDir, ec);
    auto path = configDir / (titleId + ".config.toml");
    if (!std::filesystem::exists(path)) {
        std::string s = "# Per-game configuration for Title ID " + titleId + ".\n";
        s += "# Only override the options you need; everything else inherits from the global config.\n";
        stringToFile(path, s);
    }
    for (size_t i = 0; i < openFiles.size(); i++) {
        if (openFiles[i].path == path) {
            selectedFile = (int)i;
            editingPerGame = true;
            target.load(path);
            rebuild();
            return;
        }
    }
    FileEntry fe;
    fe.label = path.filename().string();
    fe.path = path;
    openFiles.insert(openFiles.end() - 1, fe);
    selectedFile = (int)openFiles.size() - 2;
    editingPerGame = true;
    target.load(path);
    statusMsg = "Created per-game config: " + path.string();
    rebuild();
}

void EditorApp::exportTargetAs() {
    std::filesystem::path picked;
    if (!pickDialog(L"Save a copy of the config", false, picked)) return;
    if (stringToFile(picked, fileToString(target.path))) {
        statusMsg = "Exported to: " + picked.string();
    } else {
        statusMsg = "Export failed";
    }
}

static void drawRowTooltip(const SettingInfo& s) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(s.key.c_str());
    ImGui::Separator();
    if (!s.description.empty()) {
        ImGui::TextWrapped("%s", s.description.c_str());
        ImGui::Separator();
    }
    ImGui::TextUnformatted("Category: ");
    ImGui::SameLine();
    ImGui::TextUnformatted(s.category.c_str());
    ImGui::TextUnformatted("Type: ");
    ImGui::SameLine();
    const char* t = "raw";
    switch (s.type) {
        case ValType::Bool: t = "boolean"; break;
        case ValType::Int: t = "integer"; break;
        case ValType::Float: t = "float"; break;
        case ValType::Str: t = "string"; break;
        case ValType::Arr: t = "array"; break;
        case ValType::Raw: t = "raw"; break;
    }
    ImGui::TextUnformatted(t);
    if (!s.isEnum) {
        ImGui::TextUnformatted("Default: ");
        ImGui::SameLine();
        ImGui::TextUnformatted(s.defaultRaw.c_str());
    }
    if (s.unsafe) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.1f, 1.0f), "%s This option is for advanced users and may break things.", ICON_WARN);
    }
    if (s.isEnum) {
        ImGui::Separator();
        ImGui::TextUnformatted("Allowed values:");
        for (auto& o : s.enums) {
            ImGui::BulletText("%s%s%s", o.value.c_str(), o.desc.empty() ? "" : " - ", o.desc.c_str());
        }
    }
    ImGui::EndTooltip();
}

static void drawContextMenu(EditorApp& app, SettingInfo& s) {
    if (!ImGui::BeginPopup("##ctx")) return;
    if (ImGui::MenuItem("Copy key path (Category.key)"))
        ImGui::SetClipboardText(s.full.c_str());
    if (ImGui::MenuItem("Copy TOML line")) {
        std::string line = s.key + " = " + s.effRaw;
        ImGui::SetClipboardText(line.c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem(s.favorite ? "Remove favorite" : "Add favorite"))
        app.toggleFavorite(s.full);
    if (ImGui::MenuItem("Edit raw value...")) {
        app.rawEditTarget = s.full;
        snprintf(app.rawBuf, sizeof(app.rawBuf), "%s", s.effRaw.c_str());
        ImGui::OpenPopup("##rawedit");
    }
    if (s.isPath) {
        if (ImGui::MenuItem("Browse...")) {
            std::filesystem::path picked;
            if (pickDialog(L"Select path", s.pathIsFolder, picked)) {
                std::string v = s.type == ValType::Str ? encodeStr(picked.string()) : picked.string();
                app.applyEdit(s, v);
            }
        }
    }
    if (s.modified) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reset to default")) app.resetSetting(s);
        if (app.editingPerGame && s.present && ImGui::MenuItem("Remove override"))
            app.removeOverride(s);
    }
    ImGui::EndPopup();
}

void EditorApp::drawRow(SettingInfo& s) {
    ImGui::PushID(s.full.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    float favW = 26.0f;
    float dotW = 20.0f;
    if (ImGui::Button(s.favorite ? ICON_FAV : ICON_FAV_OFF, ImVec2(favW, 0))) toggleFavorite(s.full);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(s.favorite ? "Remove from favorites" : "Add to favorites");
        ImGui::EndTooltip();
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (s.modified) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "%s", ICON_DOT);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), s.modified ? "Changed from default" : "");
            ImGui::EndTooltip();
        }
    } else {
        ImGui::TextDisabled("%s", u8"\u00B7");
    }
    ImGui::SameLine(0.0f, 4.0f);

    std::string label = s.key;
    if (s.unsafe) label = std::string(ICON_WARN) + " " + label;
    float avail = ImGui::GetContentRegionAvail().x;
    float labelW = avail - 8.0f;
    if (labelW > 0) {
        int maxBytes = (int)(labelW / 7.5f);
        if (maxBytes > 0 && (int)label.size() > maxBytes) label = truncateUtf8(label, maxBytes);
    }
    ImGui::Text("%s", label.c_str());
    if (ImGui::IsItemHovered()) {
        drawRowTooltip(s);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##ctx");
    }
    drawContextMenu(*this, s);

    ImGui::TableNextColumn();

    bool perGameDisabled = editingPerGame && !s.present;
    if (perGameDisabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
        if (ImGui::Button("Enable override", ImVec2(-1, 0))) applyEdit(s, s.baseRaw);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Not in this per-game config - inherited from the global config.");
            ImGui::TextUnformatted("Click to override it here.");
            ImGui::EndTooltip();
        }
    } else {
        switch (s.type) {
            case ValType::Bool: {
                bool v = s.effRaw == "true";
                if (ImGui::Checkbox("##v", &v)) applyEdit(s, v ? "true" : "false");
                break;
            }
            case ValType::Int: {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputScalar("##v", ImGuiDataType_S64, &s.iVal);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    applyEdit(s, normalizeInt(std::to_string(s.iVal)));
                break;
            }
            case ValType::Float: {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputScalar("##v", ImGuiDataType_Double, &s.fVal, nullptr, nullptr, "%.9g");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "%.9g", s.fVal);
                    applyEdit(s, normalizeFloat(tmp));
                }
                break;
            }
            case ValType::Str: {
                if (s.isEnum) {
                    std::string cur = decodeStr(s.effRaw);
                    int sel = -1;
                    for (size_t i = 0; i < s.enums.size(); i++)
                        if (s.enums[i].value == cur) sel = (int)i;
                    bool custom = sel < 0;
                    std::vector<std::string> labels;
                    if (custom) labels.push_back(cur);
                    for (auto& o : s.enums) labels.push_back(o.label);
                    std::vector<const char*> items;
                    for (auto& l : labels) items.push_back(l.c_str());
                    int shown = custom ? 0 : sel;
                    ImGui::SetNextItemWidth(-1.0f);
                    int before = shown;
                    ImGui::Combo("##v", &shown, items.data(), (int)items.size());
                    if (shown != before && !(custom && shown == 0)) {
                        size_t idx = custom ? (size_t)shown - 1 : (size_t)shown;
                        applyEdit(s, encodeStr(s.enums[idx].value));
                    }
                } else if (s.isPath) {
                    ImGui::SetNextItemWidth(-64.0f);
                    bool entered = ImGui::InputText("##v", s.buf, sizeof(s.buf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
                    bool deact = ImGui::IsItemDeactivatedAfterEdit();
                    if (entered || deact) applyEdit(s, encodeStr(std::string(s.buf)));
                    ImGui::SameLine();
                    if (ImGui::Button("Browse", ImVec2(58, 0))) {
                        std::filesystem::path picked;
                        if (pickDialog(L"Select path", s.pathIsFolder, picked))
                            applyEdit(s, encodeStr(picked.string()));
                    }
                } else {
                    ImGui::SetNextItemWidth(-1.0f);
                    bool entered = ImGui::InputText("##v", s.buf, sizeof(s.buf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
                    bool deact = ImGui::IsItemDeactivatedAfterEdit();
                    if (entered || deact) applyEdit(s, encodeStr(std::string(s.buf)));
                }
                break;
            }
            case ValType::Arr:
            case ValType::Raw: {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextMultiline("##v", s.buf, sizeof(s.buf), ImVec2(-1, 0));
                if (ImGui::IsItemDeactivatedAfterEdit()) applyEdit(s, std::string(s.buf));
                break;
            }
        }
    }

    if (s.present && s.modified) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 0.6f));
        if (ImGui::Button(ICON_RESET)) resetSetting(s);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(editingPerGame ? "Remove override" : "Reset to default");
            ImGui::EndTooltip();
        }
    }

    ImGui::PopID();
}

void EditorApp::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Config File...", "Ctrl+O")) browseOpenFile();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, target.dirty())) wantSave = true;
            if (ImGui::MenuItem("Save Copy As...")) exportTargetAs();
            if (ImGui::MenuItem("Reload from Disk")) wantReload = true;
            ImGui::Separator();
            if (ImGui::MenuItem("New Per-Game Config...")) newPerGameOpen = true;
            if (ImGui::MenuItem("Set Xenia Folder...")) {
                std::filesystem::path picked;
                if (pickDialog(L"Select your Xenia folder", true, picked)) setXeniaDir(picked);
            }
            if (ImGui::MenuItem("Set Defaults File...")) {
                std::filesystem::path picked;
                if (pickDialog(L"Select a fresh Xenia config to use as defaults", false, picked)) {
                    defaultsPath = picked;
                    defaults.load(picked);
                    saveAppSettings();
                    rebuild();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Only changed", nullptr, &onlyChanged);
            ImGui::MenuItem("Only favorites", nullptr, &onlyFavorites);
            ImGui::MenuItem("Only unsafe/experimental", nullptr, &onlyUnsafe);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset current category"))
                for (auto& s : settings)
                    if (s.category == selectedCategory && s.modified) resetSetting(s);
            if (ImGui::MenuItem("Reset all changed...")) confirmResetAll = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorApp::draw() {
    ImGuiIO& io = ImGui::GetIO();
    // The Library tab is driven entirely by the manual controller cursor
    // (tabs, toolbar, tiles and the details sidebar are one flat target list).
    // Turn native gamepad nav off there so ImGui can't steal D-pad/A or move
    // focus on its own; the other tabs keep normal native gamepad navigation,
    // and popups on the Library tab re-enable it so dialogs stay usable.
    const bool libraryPopup = tab == 0 && ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
    if (tab == 0 && !libraryPopup)
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    else
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // Advance the manual cursor before the tab bar is drawn so the tab targets
    // highlight on the exact frame the user moves onto them.
    if (tab == 0) updateLibraryNav();
    monitorRunning();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) wantSave = true;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) browseOpenFile();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) focusSearch = true;

    drawMenuBar();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##app", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNavFocus);

    if (ImGui::BeginTabBar("maintabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
        if (ImGui::BeginTabItem("Library", nullptr, tab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            tab = 0;
            ImGui::EndTabItem();
        }
        if (tab == 0 && libNavIndex == kNavTabLibrary) drawNavCursorRect();
        if (ImGui::BeginTabItem("Config Editor", nullptr, tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            tab = 1;
            ImGui::EndTabItem();
        }
        if (tab == 0 && libNavIndex == kNavTabConfig) drawNavCursorRect();
        if (ImGui::BeginTabItem("Compatibility", nullptr, tab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            tab = 2;
            ImGui::EndTabItem();
        }
        if (tab == 0 && libNavIndex == kNavTabCompat) drawNavCursorRect();
        ImGui::EndTabBar();
    }

    ImGui::BeginChild("mainbody", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    if (tab == 0)
        drawLibrary();
    else if (tab == 1)
        drawConfigEditor();
    else
        drawCompat();
    ImGui::EndChild();

    drawAddDialog();
    drawEditDialog();
    drawRemoveConfirm();
    drawScanDialog();
    drawImportDialog();

    ImGui::End();
}

void EditorApp::drawConfigEditor() {
    if (ImGui::Button("Save", ImVec2(70, 0)) || wantSave) {
        wantSave = false;
        saveTarget();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+S");
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(70, 0)) || wantReload) {
        wantReload = false;
        target.load(target.path);
        statusMsg.clear();
        rebuild();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Only changed", &onlyChanged);
    ImGui::SameLine();
    ImGui::Checkbox("Only favorites", &onlyFavorites);
    ImGui::SameLine();
    ImGui::Checkbox("Only unsafe", &onlyUnsafe);
    ImGui::SameLine();
    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    if (focusSearch) {
        ImGui::SetKeyboardFocusHere();
        focusSearch = false;
    }
    ImGui::SetNextItemWidth(230.0f);
    ImGui::InputText("##search", &search);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) search.clear();

    ImGui::Separator();

    float leftW = 300.0f;
    ImGui::BeginChild("left", ImVec2(leftW, -24.0f), true);
    ImGui::TextUnformatted("Config file:");
    ImGui::SameLine();
    {
        std::vector<const char*> items;
        for (auto& fe : openFiles) items.push_back(fe.label.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        int sel = selectedFile;
        if (ImGui::Combo("##files", &sel, items.data(), (int)items.size())) switchFile(sel);
    }
    if (editingPerGame) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                           "%s Only a subset of options can be overridden per-game - most emulator subsystems initialize before they load.", ICON_WARN);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Categories");
    {
        int totalMod = 0;
        for (auto& s : settings)
            if (s.modified) totalMod++;
        bool selAll = selectedCategory.empty();
        if (ImGui::Selectable("All settings", selAll)) selectedCategory.clear();
        ImGui::SameLine();
        ImGui::TextDisabled("(%d/%d)", totalMod, (int)settings.size());
    }
    for (auto& cat : categories) {
        int count = 0, mod = 0;
        for (auto& s : settings) {
            if (s.category != cat) continue;
            count++;
            if (s.modified) mod++;
        }
        if (count == 0) continue;
        bool sel = selectedCategory == cat;
        if (ImGui::Selectable(cat.c_str(), sel)) selectedCategory = cat;
        if (mod > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "(%d)", mod);
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Favorites");
    if (favorites.empty()) {
        ImGui::TextDisabled("Right-click a setting to favorite it");
    } else {
        for (auto& s : settings) {
            if (!s.favorite) continue;
            if (ImGui::Selectable(s.key.c_str(), false)) {
                selectedCategory.clear();
                search = s.full;
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("main", ImVec2(-1.0f, -24.0f), false);
    if (!defaults.loaded) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                           "%s Defaults file not found (%s). Use File > Set Defaults File... to select a fresh Xenia config.", ICON_WARN,
                           defaultsPath.string().c_str());
        ImGui::EndChild();
        return;
    }
    std::string title = selectedCategory.empty() ? "All settings" : selectedCategory;
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", target.path.string().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Reset category", ImVec2(120, 0)))
        for (auto& s : settings)
            if (s.category == selectedCategory && s.modified) resetSetting(s);
    ImGui::SameLine();
    if (ImGui::Button("Reset all changed", ImVec2(130, 0))) confirmResetAll = true;
    ImGui::Separator();

    std::vector<SettingInfo*> shown;
    for (auto& s : settings) {
        if (!selectedCategory.empty() && s.category != selectedCategory) continue;
        if (onlyChanged && !s.modified) continue;
        if (onlyFavorites && !s.favorite) continue;
        if (onlyUnsafe && !s.unsafe) continue;
        if (!search.empty()) {
            bool k = containsIgnoreCase(s.key, search);
            bool d = containsIgnoreCase(s.description, search);
            if (!k && !d) continue;
        }
        shown.push_back(&s);
    }

    ImGui::BeginChild("settings", ImVec2(0, -28.0f), true);
    if (ImGui::BeginTable("settings", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("setting", ImGuiTableColumnFlags_WidthFixed, 460.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (auto* s : shown) drawRow(*s);
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::TextDisabled("Showing %d of %d settings", (int)shown.size(), (int)settings.size());
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(8, ImGui::GetWindowSize().y - 20.0f));
    ImGui::TextUnformatted(targetLabel().c_str());
    if (!statusMsg.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", statusMsg.c_str());
    }
    if (settingsDirty) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "   unsaved changes");
    }

    if (newPerGameOpen) {
        ImGui::OpenPopup("New per-game config");
        newPerGameOpen = false;
    }
    if (ImGui::BeginPopupModal("New per-game config", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Title ID (8 hex characters):");
        ImGui::InputText("##gameid", gameIdBuf, sizeof(gameIdBuf));
        bool ok = strlen(gameIdBuf) > 0;
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button("Create")) {
            createPerGame(std::string(gameIdBuf));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (confirmResetAll) {
        ImGui::OpenPopup("Reset all");
        confirmResetAll = false;
    }
    if (ImGui::BeginPopupModal("Reset all", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Reset all modified settings to defaults?");
        if (ImGui::Button("Yes")) {
            for (auto& s : settings)
                if (s.modified) resetSetting(s);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (showAbout) {
        ImGui::OpenPopup("About");
        showAbout = false;
    }
    if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("XeLauncher");
        ImGui::TextUnformatted("Right-click any setting for options.");
        ImGui::TextUnformatted("Ctrl+S save, Ctrl+O open, Ctrl+F search.");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorApp::beginAddGameFiles() {
    std::vector<std::filesystem::path> files;
    if (!pickGameFiles(files)) return;
    addQueue = files;
    advanceAddQueue();
}

void EditorApp::beginAddGameFolder() {
    std::filesystem::path dir;
    if (!pickDialog(L"Add game folder", true, dir)) return;
    addQueue = {dir};
    advanceAddQueue();
}

void EditorApp::beginFolderScan() {
    std::filesystem::path dir;
    if (!pickDialog(L"Scan folder for games", true, dir)) return;
    scanQueue.clear();
    scanDone = scanAdded = scanSkipped = 0;
    std::error_code ec;
    std::set<std::string> seen;
    for (auto& de : std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        bool isDir = de.is_directory(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (isDir) continue;
        std::string ext = lowerStr(de.path().extension().string());
        if (ext != ".iso" && ext != ".xex" && ext != ".zar" && ext != ".xbe" && ext != ".god")
            continue;
        std::string key = de.path().string();
        if (seen.insert(key).second) scanQueue.push_back(de.path());
    }
    scanTotal = scanQueue.size();
    if (scanTotal == 0) {
        libStatus = "No game files found in that folder.";
        libStatusErr = "1";
        return;
    }
    scanDialogOpen = true;
}

void EditorApp::advanceScan() {
    int processedThisFrame = 0;
    while (scanDone < scanTotal && processedThisFrame < 6) {
        const std::filesystem::path& p = scanQueue[scanDone];
        scanDone++;
        processedThisFrame++;
        std::string key = p.string();
        if (library.find(key)) {
            scanSkipped++;
            continue;
        }
        GameEntry g;
        g.path = key;
        g.type = "file";
        std::string ext = lowerStr(p.extension().string());
        if (ext == ".xex") g.type = "xex";
        else if (ext == ".xbe") g.type = "xbe";
        else if (ext == ".iso") g.type = "iso";
        else if (ext == ".god") g.type = "god";
        std::string tid = detectTitleIdFromFile(p);
        g.titleId = tid;
        if (!tid.empty()) {
            CompatEntry ce = compat.find(tid);
            if (!ce.title.empty()) g.name = ce.title;
        }
        if (g.name.empty()) g.name = guessGameName(key);
        library.add(g);
        scanAdded++;
    }
    if (scanDone >= scanTotal) {
        library.save();
        scanDialogOpen = false;
        scanQueue.clear();
        libStatus = "Scan complete: added " + std::to_string(scanAdded) + " games, skipped " +
                    std::to_string(scanSkipped);
        libStatusErr.clear();
    }
}

void EditorApp::advanceAddQueue() {
    if (addQueue.empty()) {
        addDialogOpen = false;
        return;
    }
    std::filesystem::path p = addQueue.front();
    addQueue.erase(addQueue.begin());
    addPath = p.string();
    addType = std::filesystem::is_directory(p) ? "folder" : "file";
    if (!std::filesystem::is_directory(p)) {
        std::string ext = lowerStr(p.extension().string());
        if (ext == ".xex") addType = "xex";
        else if (ext == ".xbe") addType = "xbe";
        else if (ext == ".iso") addType = "iso";
        else if (ext == ".god") addType = "god";
    }
    std::string tid = detectTitleIdFromFile(p);
    std::string name;
    if (!tid.empty()) {
        CompatEntry ce = compat.find(tid);
        if (!ce.title.empty()) name = ce.title;
    }
    if (name.empty()) name = guessGameName(addPath);
    snprintf(addNameBuf, sizeof(addNameBuf), "%s", name.c_str());
    snprintf(addTitleBuf, sizeof(addTitleBuf), "%s", tid.c_str());
    addCover.clear();
    addDialogOpen = true;
}

void EditorApp::finishAddGame() {
    GameEntry g;
    g.name = addNameBuf;
    g.titleId = addTitleBuf;
    g.path = addPath;
    g.type = addType;
    g.cover = addCover;
    library.add(g);
    library.save();
    libStatus = "Added: " + g.name;
    libStatusErr.clear();
    advanceAddQueue();
}

void EditorApp::launchGame(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    // Debounce: a held/queued gamepad A (or a double-press across a focus
    // change) must not relaunch a game immediately after one was started.
    if (steadyMs() - lastLaunchMs < 1000) return;
    lastLaunchMs = steadyMs();
    GameEntry& g = library.games[idx];
    if (g.path.empty()) {
        libStatus = g.name + " has no game file yet - click Edit to set one.";
        libStatusErr = "1";
        return;
    }
    std::filesystem::path xexe;
    const wchar_t* cands[] = {L"xenia_canary.exe", L"xenia-canary.exe", L"xenia.exe"};
    if (!xeniaDir.empty()) {
        for (auto* c : cands) {
            auto p = xeniaDir / c;
            if (std::filesystem::exists(p)) {
                xexe = p;
                break;
            }
        }
    }
    if (xexe.empty()) {
        libStatus = "Xenia executable not found - set the Xenia folder first (File > Set Xenia Folder)";
        libStatusErr = "1";
        return;
    }
    std::wstring cmd = L"\"" + xexe.wstring() + L"\" \"" + utf8ToWide(g.path) + L"\"";
    if (!g.launchArgs.empty()) cmd += L" " + utf8ToWide(g.launchArgs);
    std::vector<wchar_t> buf(cmd.size() + 1);
    memcpy(buf.data(), cmd.data(), (cmd.size() + 1) * sizeof(wchar_t));
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       xeniaDir.wstring().c_str(), &si, &pi)) {
        monitorRunning();  // finalize any previously running game first
        if (runningProc) CloseHandle((HANDLE)runningProc);
        runningProc = pi.hProcess;
        runningIdx = idx;
        runningStartMs = steadyMs();
        CloseHandle(pi.hThread);
        g.launches++;
        g.lastPlayed = (long long)time(nullptr);
        library.save();
        // Get the launcher out of the way and stop it from capturing the
        // gamepad while xenia is running.
        if (hwnd) ShowWindow((HWND)hwnd, SW_MINIMIZE);
        libStatus = "Launched: " + g.name;
        libStatusErr.clear();
    } else {
        libStatus = "Failed to launch: " + g.name;
        libStatusErr = "1";
    }
}

void EditorApp::monitorRunning() {
    if (!runningProc) return;
    DWORD code = 0;
    if (!GetExitCodeProcess((HANDLE)runningProc, &code) || code != STILL_ACTIVE) {
        long long sec = std::max<long long>(0, (steadyMs() - runningStartMs) / 1000);
        CloseHandle((HANDLE)runningProc);
        runningProc = nullptr;
        if (runningIdx >= 0 && runningIdx < (int)library.games.size()) {
            library.games[runningIdx].playtimeSec += sec;
            library.save();
            libStatus = "Played " + library.games[runningIdx].name + " for " +
                        formatPlaytime(library.games[runningIdx].playtimeSec);
            libStatusErr.clear();
        }
        runningIdx = -1;
    }
}

void EditorApp::openPerGameConfig(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    std::string tid = library.games[idx].titleId;
    if (tid.empty()) {
        libStatus = "This game has no title ID - edit it to set one before using a per-game config.";
        libStatusErr = "1";
        return;
    }
    createPerGame(tid);
    tab = 1;
}

void EditorApp::toggleGameFav(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    library.games[idx].fav = !library.games[idx].fav;
    library.save();
}

void EditorApp::gameContextMenu(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    GameEntry& g = library.games[idx];
    if (ImGui::BeginPopupContextItem("gamectx")) {
        if (ImGui::MenuItem("Launch")) launchGame(idx);
        if (ImGui::MenuItem(g.fav ? "Remove from favorites" : "Add to favorites"))
            toggleGameFav(idx);
        if (ImGui::MenuItem("Create Desktop Shortcut")) createDesktopShortcut(idx);
        ImGui::Separator();
        if (ImGui::MenuItem("Set Cover Image...")) pickCoverFor(idx);
        if (ImGui::MenuItem("Download Cover")) downloadCoverFor(idx);
        if (ImGui::MenuItem("Edit Title / Name / Path...")) openEditGame(idx);
        if (ImGui::MenuItem("Open Per-Game Config")) openPerGameConfig(idx);
        if (ImGui::MenuItem("Open Containing Folder")) {
            std::filesystem::path dir = std::filesystem::path(g.path).parent_path();
            if (dir.empty()) dir = ".";
            ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove from Library")) confirmRemoveIdx = idx;
        ImGui::EndPopup();
    }
}

void EditorApp::createDesktopShortcut(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    GameEntry& g = library.games[idx];
    std::filesystem::path xexe;
    const wchar_t* cands[] = {L"xenia_canary.exe", L"xenia-canary.exe", L"xenia.exe"};
    if (!xeniaDir.empty()) {
        for (auto* c : cands) {
            auto p = xeniaDir / c;
            if (std::filesystem::exists(p)) {
                xexe = p;
                break;
            }
        }
    }
    if (xexe.empty()) {
        libStatus = "Xenia executable not found - set the Xenia folder first.";
        libStatusErr = "1";
        return;
    }
    if (g.path.empty()) {
        libStatus = "This game has no game file yet - set one in Edit first.";
        libStatusErr = "1";
        return;
    }
    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)) || !desktop) {
        libStatus = "Could not locate the desktop folder.";
        libStatusErr = "1";
        return;
    }
    std::filesystem::path lnk =
        std::filesystem::path(desktop) / (sanitizeFileName(g.name) + ".lnk");
    CoTaskMemFree(desktop);

    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) {
        libStatus = "Failed to create shortcut (COM error).";
        libStatusErr = "1";
        return;
    }
    link->SetPath(xexe.wstring().c_str());
    link->SetArguments((L"\"" + utf8ToWide(g.path) + L"\"").c_str());
    link->SetWorkingDirectory(xeniaDir.wstring().c_str());
    link->SetDescription(utf8ToWide(g.name).c_str());
    ComPtr<IPersistFile> pf;
    if (SUCCEEDED(link.As(&pf)) && SUCCEEDED(pf->Save(lnk.wstring().c_str(), TRUE))) {
        libStatus = "Shortcut created: " + lnk.string();
        libStatusErr.clear();
    } else {
        libStatus = "Failed to write shortcut file.";
        libStatusErr = "1";
    }
}

void EditorApp::openImportDialog() {
    importDialogOpen = true;
    importSearch.clear();
    importSelId.clear();
}

void EditorApp::importByTitleId() {
    if (importSelId.empty()) return;
    CompatEntry ce = compat.find(importSelId);
    if (ce.id.empty()) return;
    GameEntry g;
    g.name = ce.title.empty() ? importSelId : ce.title;
    g.titleId = ce.id;
    g.type = "file";
    library.add(g);
    library.save();
    importDialogOpen = false;
    importSelId.clear();
    libStatus = "Added " + g.name + " - set its game file in Edit to enable launching.";
    libStatusErr.clear();
}

void EditorApp::downloadCoverTo(const std::string& name, const std::string& titleId,
                                std::string& out) {
    ensureLaunchBoxCached();
    std::error_code ec;
    if (!std::filesystem::exists(library.dataDir / "launchbox.json", ec)) {
        libStatus = "Cover metadata is downloading in the background - try again in a few seconds.";
        libStatusErr = "1";
        return;
    }
    std::string url;
    if (!lookupCoverUrl(library.dataDir, name, url)) {
        libStatus = "No cover found online for '" + name + "'";
        libStatusErr = "1";
        return;
    }
    std::string ext = ".jpg";
    std::string lurl = lowerStr(url);
    if (lurl.find(".png") != std::string::npos) ext = ".png";
    else if (lurl.find(".jpeg") != std::string::npos) ext = ".jpg";
    std::string base = sanitizeFileName(titleId.empty() ? name : titleId);
    auto coversDir = library.dataDir / "covers";
    std::error_code ec2;
    std::filesystem::create_directories(coversDir, ec2);
    std::string dst = (coversDir / (base + ext)).string();
    if (httpDownloadFile(utf8ToWide(url), dst)) {
        out = dst;
        libStatus = "Cover downloaded for " + name;
        libStatusErr.clear();
    } else {
        libStatus = "Failed to download cover for " + name;
        libStatusErr = "1";
    }
}

void EditorApp::downloadCoverForAdd() {
    std::string name = addNameBuf;
    if (name.empty()) return;
    downloadCoverTo(name, addTitleBuf, addCover);
}

void EditorApp::downloadCoverFor(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    GameEntry& g = library.games[idx];
    downloadCoverTo(g.name, g.titleId, g.cover);
    library.save();
}

void EditorApp::pickCoverFor(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    std::filesystem::path p;
    if (!pickImageFile(p)) return;
    library.games[idx].cover = p.string();
    library.save();
}

void EditorApp::openEditGame(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    snprintf(editNameBuf, sizeof(editNameBuf), "%s", library.games[idx].name.c_str());
    snprintf(editTitleBuf, sizeof(editTitleBuf), "%s", library.games[idx].titleId.c_str());
    snprintf(editPathBuf, sizeof(editPathBuf), "%s", library.games[idx].path.c_str());
    snprintf(editArgsBuf, sizeof(editArgsBuf), "%s", library.games[idx].launchArgs.c_str());
    editGameIdx = idx;
    editDialogOpen = true;
}

void EditorApp::saveEditGame() {
    if (editGameIdx < 0 || editGameIdx >= (int)library.games.size()) return;
    GameEntry& g = library.games[editGameIdx];
    g.name = editNameBuf;
    g.titleId = editTitleBuf;
    g.path = editPathBuf;
    g.launchArgs = editArgsBuf;
    std::string ext = lowerStr(std::filesystem::path(g.path).extension().string());
    if (g.path.empty())
        g.type = "file";
    else if (std::filesystem::is_directory(g.path))
        g.type = "folder";
    else if (ext == ".xex") g.type = "xex";
    else if (ext == ".xbe") g.type = "xbe";
    else if (ext == ".iso") g.type = "iso";
    else if (ext == ".god") g.type = "god";
    else
        g.type = "file";
    library.save();
    editDialogOpen = false;
    editGameIdx = -1;
}

void EditorApp::removeGame(int idx) {
    if (idx < 0 || idx >= (int)library.games.size()) return;
    std::string n = library.games[idx].name;
    library.remove(idx);
    library.save();
    if (selectedGame >= (int)library.games.size()) selectedGame = -1;
    libStatus = "Removed: " + n;
    libStatusErr.clear();
}

void EditorApp::updateLibraryNav() {
    // Build the visible, sorted list the cursor moves over. Done before the
    // tab bar is drawn so the cursor has no one-frame highlight lag.
    libShown.clear();
    if (!library.games.empty()) {
        for (int i = 0; i < (int)library.games.size(); i++) {
            GameEntry& g = library.games[i];
            if (!libSearch.empty() && !containsIgnoreCase(g.name, libSearch) &&
                !containsIgnoreCase(g.titleId, libSearch))
                continue;
            if (libOnlyFavorites && !g.fav) continue;
            CompatEntry ce = compat.find(g.titleId);
            std::string state = ce.state;
            bool hasData = !state.empty();
            if (libCompatFilter == 1 && state != "Playable") continue;
            if (libCompatFilter == 2 && state != "Gameplay") continue;
            if (libCompatFilter == 3 && state != "Loads") continue;
            if (libCompatFilter == 4 && state != "Unplayable") continue;
            if (libCompatFilter == 5 && hasData) continue;
            libShown.push_back(i);
        }
    }
    switch (libSort) {
        case 1:
            std::stable_sort(libShown.begin(), libShown.end(), [&](int a, int b) {
                return lowerStr(library.games[a].name) > lowerStr(library.games[b].name);
            });
            break;
        case 2:
            std::stable_sort(libShown.begin(), libShown.end(), [&](int a, int b) {
                return library.games[a].lastPlayed > library.games[b].lastPlayed;
            });
            break;
        case 3:
            std::stable_sort(libShown.begin(), libShown.end(), [&](int a, int b) {
                return library.games[a].launches > library.games[b].launches;
            });
            break;
        case 4:
            std::stable_sort(libShown.begin(), libShown.end(), [&](int a, int b) {
                return library.games[a].playtimeSec > library.games[b].playtimeSec;
            });
            break;
        case 5:
            std::stable_sort(libShown.begin(), libShown.end(), [&](int a, int b) {
                if (library.games[a].fav != library.games[b].fav)
                    return library.games[a].fav > library.games[b].fav;
                return lowerStr(library.games[a].name) < lowerStr(library.games[b].name);
            });
            break;
        default:
            std::stable_sort(libShown.begin(), libShown.end(), [&](int a, int b) {
                return lowerStr(library.games[a].name) < lowerStr(library.games[b].name);
            });
            break;
    }

    // The details sidebar (right panel) is shown whenever a game is selected;
    // its buttons are the tail of the cursor sequence.
    const bool showDetails = selectedGame >= 0 && selectedGame < (int)library.games.size();
    const int detailCount = showDetails ? (kNavDetailLast - kNavDetailLaunch + 1) : 0;
    const int total = kNavToolbar + (int)libShown.size() + detailCount;

    const bool modalOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
    const int prevNav = libNavIndex;
    if (libNavIndex >= total) libNavIndex = total - 1;

    if (!modalOpen) {
        // Movement is one item per step. ImGui's built-in key repeat is much
        // too fast for a gamepad UI (20 moves/sec after 0.25s), so holding a
        // D-pad direction for a normal ~0.4s press raced through ~4 tiles.
        // Repeat is driven manually instead: first move immediately, then wait
        // a delay and repeat at a calm rate. Some joystick drivers report the
        // POV as toggling off/on every other frame while the D-pad is held, so
        // short drop-outs are debounced and never reset the repeat timer.
        const bool heldDown = ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) ||
                              ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight);
        const bool heldUp = ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) ||
                            ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft);
        static const float kRepeatDelay = 0.30f;
        static const float kRepeatRate = 0.13f;
        const float dt = ImGui::GetIO().DeltaTime;

        const int holdDir = heldDown ? 1 : (heldUp ? -1 : 0);
        if (holdDir != 0) {
            libNavZeroFrames = 0;
            if (libNavHoldDir != holdDir) {
                // New direction engaged: move on the first frame.
                libNavHoldDir = holdDir;
                libNavHoldTime = 0.0f;
                libNavNextRepeat = kRepeatDelay;
            } else {
                libNavHoldTime += dt;
            }
        } else if (libNavHoldDir != 0) {
            // No direction this frame. Debounce a one-frame drop-out so a
            // flaky pad read can't reset the cursor to "fresh press" every
            // other frame (which made holding scroll at ~30 moves/sec).
            if (++libNavZeroFrames < 2) {
                libNavHoldTime += dt;
            } else {
                libNavHoldDir = 0;
                libNavHoldTime = 0.0f;
                libNavZeroFrames = 0;
            }
        }

        if (libNavHoldDir != 0) {
            bool move = false;
            if (libNavHoldTime <= 0.0f)
                move = true;  // first frame of the press
            else if (libNavHoldTime >= libNavNextRepeat) {
                move = true;
                libNavNextRepeat += kRepeatRate;
            }
            if (move) {
                if (libNavHoldDir > 0) libNavIndex = (libNavIndex + 1) % total;
                else libNavIndex = (libNavIndex - 1 + total) % total;
            }
        }
    }
    if (libNavIndex != prevNav) {
        libNavMoveFrame = ImGui::GetFrameCount();
        if (libNavIndex >= kNavToolbar) libNeedScroll = true;
    }

    const int gridIdx = libNavIndex - kNavToolbar;
    const bool cursorInGrid = gridIdx >= 0 && gridIdx < (int)libShown.size();
    if (cursorInGrid) selectedGame = libShown[gridIdx];

    // A on a tab target switches tabs (handled before the tab bar draws).
    if (!modalOpen && libNavA()) {
        if (libNavIndex == kNavTabLibrary) tab = 0;
        else if (libNavIndex == kNavTabConfig) tab = 1;
        else if (libNavIndex == kNavTabCompat) tab = 2;
    }
}

void EditorApp::drawLibrary() {
    // libShown and the cursor position were computed in updateLibraryNav()
    // before the tab bar drew, so highlights have no one-frame lag.
    const bool navA = libNavA();

    static const char* kSortLabels[] = {"Name (A-Z)", "Name (Z-A)", "Recently played",
                                        "Most played", "Playtime", "Favorites first"};
    static const char* kFilterLabels[] = {"Any status", "Playable", "Gameplay", "Loads",
                                          "Unplayable", "No compat data"};

    // ---- toolbar (all ImGui-NoNav except the search box so it can take
    // keyboard focus; driven by the cursor above) ----
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (ImGui::Button("Add Game...")) beginAddGameFiles();
    if (libNavIndex == kNavAdd && navA) beginAddGameFiles();
    if (libNavIndex == kNavAdd) drawNavCursorRect();
    ImGui::SameLine();
    if (ImGui::Button("Add Folder...")) beginAddGameFolder();
    if (libNavIndex == kNavAddFolder && navA) beginAddGameFolder();
    if (libNavIndex == kNavAddFolder) drawNavCursorRect();
    ImGui::SameLine();
    if (ImGui::Button("Scan Folder...")) beginFolderScan();
    if (libNavIndex == kNavScan && navA) beginFolderScan();
    if (libNavIndex == kNavScan) drawNavCursorRect();
    ImGui::SameLine();
    if (ImGui::Button("Import by Title ID...")) openImportDialog();
    if (libNavIndex == kNavImport && navA) openImportDialog();
    if (libNavIndex == kNavImport) drawNavCursorRect();
    ImGui::SameLine();
    if (ImGui::Button(libListView ? "Grid View" : "List View")) libListView = !libListView;
    if (libNavIndex == kNavView && navA) libListView = !libListView;
    if (libNavIndex == kNavView) drawNavCursorRect();

    ImGui::Separator();
    ImGui::PopItemFlag();

    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    // The box is only nav-able while the cursor sits on it (so A can focus it
    // for typing); otherwise it's NoNav so native gamepad nav has zero targets
    // in this window and can never fight the manual cursor. Mouse focus works
    // regardless of NoNav.
    if (libNavIndex != kNavSearch) ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (libNavIndex == kNavSearch && navA) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##libsearch", &libSearch);
    if (libNavIndex == kNavSearch) drawNavCursorRect();
    if (libNavIndex != kNavSearch) ImGui::PopItemFlag();
    ImGui::SameLine();

    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (ImGui::Button("Clear")) libSearch.clear();
    if (libNavIndex == kNavClear && navA) libSearch.clear();
    if (libNavIndex == kNavClear) drawNavCursorRect();
    ImGui::SameLine();
    ImGui::TextUnformatted("Sort:");
    ImGui::SameLine();
    if (ImGui::Button(kSortLabels[libSort], ImVec2(140.0f, 0))) libSort = (libSort + 1) % 6;
    if (libNavIndex == kNavSort && navA) libSort = (libSort + 1) % 6;
    if (libNavIndex == kNavSort) drawNavCursorRect();
    ImGui::SameLine();
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    if (ImGui::Button(kFilterLabels[libCompatFilter], ImVec2(130.0f, 0)))
        libCompatFilter = (libCompatFilter + 1) % 6;
    if (libNavIndex == kNavStatus && navA) libCompatFilter = (libCompatFilter + 1) % 6;
    if (libNavIndex == kNavStatus) drawNavCursorRect();
    ImGui::SameLine();
    ImGui::Checkbox("Only favorites", &libOnlyFavorites);
    if (libNavIndex == kNavFav && navA) libOnlyFavorites = !libOnlyFavorites;
    if (libNavIndex == kNavFav) drawNavCursorRect();
    ImGui::SameLine();
    if (ImGui::Button("Refresh compat data")) compat.startFetch();
    if (libNavIndex == kNavRefresh && navA) compat.startFetch();
    if (libNavIndex == kNavRefresh) drawNavCursorRect();
    ImGui::SameLine();
    if (compat.busy)
        ImGui::TextDisabled("Updating compatibility data...");
    else if (compat.loaded)
        ImGui::TextDisabled("Compatibility: %d titles (%s)", (int)compat.size(),
                            compat.source.c_str());
    else
        ImGui::TextDisabled("Compatibility data not loaded");
    ImGui::PopItemFlag();

    ImGui::Separator();

    if (library.games.empty()) {
        ImVec2 sz = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(sz.x * 0.5f - 140.0f, sz.y * 0.5f - 34.0f));
        ImGui::TextUnformatted("Your library is empty.");
        ImGui::SetCursorPos(ImVec2(sz.x * 0.5f - 200.0f, sz.y * 0.5f - 14.0f));
        ImGui::TextDisabled("Add a game to get started, or use Scan Folder... to import a whole collection.");
        return;
    }

    bool showDetails = selectedGame >= 0 && selectedGame < (int)library.games.size();
    ImGui::BeginChild("librarygrid", ImVec2(0, -22.0f), false, ImGuiChildFlags_NavFlattened);
    ImGui::BeginChild("libleft", ImVec2(showDetails ? -300.0f : 0.0f, 0), false, ImGuiChildFlags_NavFlattened);
    if (libShown.empty()) {
        ImGui::TextDisabled("No games match your filters.");
    } else if (libListView) {
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        if (ImGui::BeginTable("liblist", 6,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Title ID", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Last played", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Playtime", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableHeadersRow();
            int r = 0;
            for (int idx : libShown) {
                drawGameListRow(idx, libNavIndex == kNavToolbar + r);
                r++;
            }
            ImGui::EndTable();
        }
        ImGui::PopItemFlag();
    } else {
        float avail = ImGui::GetContentRegionAvail().x;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        int cols = std::max(1, (int)(avail / 165.0f));
        float tileW = (avail - spacing * (cols - 1)) / (float)cols;
        int i = 0;
        for (int idx : libShown) {
            if (i % cols != 0) ImGui::SameLine(0.0f, spacing);
            bool cur = libNavIndex == kNavToolbar + i;
            i++;
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            drawGameTile(idx, tileW, cur);
            ImGui::PopItemFlag();
        }
    }
    ImGui::EndChild();
    if (showDetails) {
        ImGui::SameLine();
        drawDetailsPanel();
    }
    ImGui::EndChild();

    if (!libStatus.empty()) {
        ImVec4 col = libStatusErr.empty() ? ImVec4(0.6f, 0.85f, 0.65f, 1.0f)
                                          : ImVec4(1.0f, 0.5f, 0.4f, 1.0f);
        ImGui::TextColored(col, "%s", libStatus.c_str());
    } else {
        ImGui::TextDisabled("Double-click to launch. Controller: stick/D-pad navigate one tile, A select.");
    }
}

void EditorApp::drawGameListRow(int idx, bool isCursor) {
    GameEntry& g = library.games[idx];
    ImGui::PushID(idx);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    bool sel = selectedGame == idx;
    std::string label = (g.fav ? std::string(ICON_FAV) + " " : "") + g.name;
    if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) {
        selectedGame = idx;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) launchGame(idx);
    }
    if (isCursor) {
        drawNavCursorRect();
        if (libNavA()) launchGame(idx);
        if (libNeedScroll) {
            // Only scroll when the cursor item is actually outside the visible
            // area; otherwise every D-pad press would re-center the grid and
            // make it jump ~half a page.
            if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
            libNeedScroll = false;
        }
    }
    gameContextMenu(idx);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", g.titleId.c_str());
    ImGui::TableSetColumnIndex(2);
    CompatEntry ce = compat.find(g.titleId);
    if (!ce.state.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, compatColor(ce.state));
        ImGui::TextUnformatted(ce.state.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("-");
    }
    ImGui::TableSetColumnIndex(3);
    ImGui::TextDisabled("%s", formatDate(g.lastPlayed).c_str());
    ImGui::TableSetColumnIndex(4);
    if (g.playtimeSec) ImGui::TextDisabled("%s", formatPlaytime(g.playtimeSec).c_str());
    ImGui::TableSetColumnIndex(5);
    if (ImGui::SmallButton("Play")) launchGame(idx);
    ImGui::PopID();
}

void EditorApp::drawDetailsPanel() {
    if (selectedGame < 0 || selectedGame >= (int)library.games.size()) return;
    GameEntry& g = library.games[selectedGame];
    // The details buttons are the tail of the manual cursor sequence. The
    // child keeps ImGuiWindowFlags_NoNav so native gamepad nav can never land
    // on "Launch" and launch the game while the cursor is elsewhere.
    ImGui::BeginChild("details", ImVec2(300, 0), true, ImGuiWindowFlags_NoNav);
    ID3D11ShaderResourceView* srv = tex.get(g.cover);
    if (srv) {
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::Image(srv, ImVec2(avail, avail * 1.4f));
    } else {
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("nocover", ImVec2(avail, avail * 1.4f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetItemRectMin();
        ImVec2 p1 = ImGui::GetItemRectMax();
        dl->AddRectFilled(p0, p1, IM_COL32(28, 28, 30, 255));
        dl->AddRect(p0, p1, IM_COL32(45, 45, 50, 255));
        std::string letter = g.name.empty() ? "?" : g.name.substr(0, 1);
        ImVec2 ts = ImGui::CalcTextSize(letter.c_str());
        dl->AddText(ImVec2(p0.x + (p1.x - p0.x - ts.x) * 0.5f,
                           p0.y + (p1.y - p0.y - ts.y) * 0.5f),
                    IM_COL32(120, 120, 130, 255), letter.c_str());
    }
    ImGui::TextUnformatted(g.name.c_str());
    if (!g.titleId.empty()) ImGui::TextDisabled("Title ID: %s", g.titleId.c_str());
    if (!g.type.empty()) ImGui::TextDisabled("Type: %s", g.type.c_str());
    CompatEntry ce = compat.find(g.titleId);
    if (!ce.state.empty()) {
        ImGui::TextUnformatted("Compatibility: ");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, compatColor(ce.state));
        ImGui::TextUnformatted(ce.state.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Compatibility: no data");
    }
    if (g.lastPlayed) ImGui::TextDisabled("Last played: %s", formatDate(g.lastPlayed).c_str());
    ImGui::TextDisabled("Launches: %d", g.launches);
    if (g.playtimeSec) ImGui::TextDisabled("Playtime: %s", formatPlaytime(g.playtimeSec).c_str());
    if (runningIdx == selectedGame && runningProc)
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "Running now");
    ImGui::Separator();

    // Flat-sequence index of the first details button; the kNavDetailX values
    // plus this base give the real cursor index for each button.
    const int dBase = kNavToolbar + (int)libShown.size() - kNavDetailLaunch;
    const bool navA = libNavA();
    auto cursorBtn = [&](int slot, bool enabled, const std::function<void()>& fn) {
        if (libNavIndex == slot) {
            if (libNeedScroll) {
                if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
                libNeedScroll = false;
            }
            if (enabled && navA) fn();
            drawNavCursorRect();
        }
    };
    auto openFolder = [&]() {
        std::filesystem::path dir = std::filesystem::path(g.path).parent_path();
        if (dir.empty()) dir = ".";
        ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };

    float bw = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("Launch", ImVec2(bw, 0))) launchGame(selectedGame);
    cursorBtn(kNavDetailLaunch + dBase, true, [&]() { launchGame(selectedGame); });
    if (ImGui::Button(g.fav ? "Remove from favorites" : "Add to favorites", ImVec2(bw, 0)))
        toggleGameFav(selectedGame);
    cursorBtn(kNavDetailFav + dBase, true, [&]() { toggleGameFav(selectedGame); });
    if (ImGui::Button("Create Desktop Shortcut", ImVec2(bw, 0)))
        createDesktopShortcut(selectedGame);
    cursorBtn(kNavDetailShortcut + dBase, true, [&]() { createDesktopShortcut(selectedGame); });
    if (ImGui::Button("Edit...", ImVec2(bw, 0))) openEditGame(selectedGame);
    cursorBtn(kNavDetailEdit + dBase, true, [&]() { openEditGame(selectedGame); });
    ImGui::BeginDisabled(g.titleId.empty());
    if (ImGui::Button("Open Per-Game Config", ImVec2(bw, 0))) openPerGameConfig(selectedGame);
    ImGui::EndDisabled();
    cursorBtn(kNavDetailPerGame + dBase, !g.titleId.empty(), [&]() { openPerGameConfig(selectedGame); });
    if (ImGui::Button("Download Cover", ImVec2(bw, 0))) downloadCoverFor(selectedGame);
    cursorBtn(kNavDetailCover + dBase, true, [&]() { downloadCoverFor(selectedGame); });
    if (ImGui::Button("Set Cover Image...", ImVec2(bw, 0))) pickCoverFor(selectedGame);
    cursorBtn(kNavDetailSetCover + dBase, true, [&]() { pickCoverFor(selectedGame); });
    ImGui::BeginDisabled(g.path.empty());
    if (ImGui::Button("Open Containing Folder", ImVec2(bw, 0))) openFolder();
    ImGui::EndDisabled();
    cursorBtn(kNavDetailFolder + dBase, !g.path.empty(), openFolder);
    if (ImGui::Button("Remove from Library", ImVec2(bw, 0))) confirmRemoveIdx = selectedGame;
    cursorBtn(kNavDetailRemove + dBase, true, [&]() { confirmRemoveIdx = selectedGame; });
    ImGui::EndChild();
}

void EditorApp::drawGameTile(int idx, float w, bool isCursor) {
    GameEntry& g = library.games[idx];
    float h = w * 1.4f;
    ImGui::BeginGroup();
    ImGui::PushID(idx);

    ImGui::InvisibleButton("cover", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    bool dbl = ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
               ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
        selectedGame = idx;
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    if (isCursor) {
        drawNavCursorRect();
        if (libNavA()) launchGame(idx);
        if (libNeedScroll) {
            // Only scroll when the cursor item is actually outside the visible
            // area; otherwise every D-pad press would re-center the grid and
            // make it jump ~half a page.
            if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
            libNeedScroll = false;
        }
    }

    if (dbl) launchGame(idx);
    gameContextMenu(idx);

    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool sel = selectedGame == idx;

    ID3D11ShaderResourceView* srv = tex.get(g.cover);
    if (srv) {
        dl->AddImage(srv, p0, p1);
    } else {
        dl->AddRectFilled(p0, p1, IM_COL32(28, 28, 30, 255));
        dl->AddRect(p0, p1, IM_COL32(45, 45, 50, 255));
        std::string letter;
        if (!g.name.empty()) letter = g.name.substr(0, 1);
        if (!letter.empty()) {
            ImVec2 ts = ImGui::CalcTextSize(letter.c_str());
            dl->AddText(ImVec2(p0.x + (p1.x - p0.x - ts.x) * 0.5f,
                               p0.y + (p1.y - p0.y - ts.y) * 0.5f),
                        IM_COL32(120, 120, 130, 255), letter.c_str());
        }
    }
    if (hovered && !active) dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 90), 0, 0, 2.0f);
    if (sel) dl->AddRect(p0, p1, IM_COL32(255, 182, 39, 255), 0, 0, 3.0f);
    if (g.fav) dl->AddText(ImVec2(p1.x - 22.0f, p0.y + 4), IM_COL32(255, 210, 60, 255), ICON_FAV);

    CompatEntry ce = compat.find(g.titleId);
    if (!ce.state.empty()) {
        ImVec2 ts = ImGui::CalcTextSize(ce.state.c_str());
        ImVec2 bp0(p0.x + 4, p0.y + 4);
        ImVec2 bp1(bp0.x + ts.x + 10, bp0.y + ts.y + 5);
        dl->AddRectFilled(bp0, bp1, IM_COL32(0, 0, 0, 190), 3.0f);
        dl->AddRect(bp0, bp1, compatColor(ce.state), 3.0f, 0, 1.5f);
        dl->AddText(ImVec2(bp0.x + 5, bp0.y + 2), compatColor(ce.state), ce.state.c_str());
    }

    float nameW = w;
    int maxChars = (int)(nameW / (ImGui::GetFontSize() * 0.55f));
    ImGui::TextUnformatted(truncateUtf8(g.name, maxChars).c_str());
    if (!g.titleId.empty()) ImGui::TextDisabled("%s", g.titleId.c_str());
    if (g.playtimeSec) ImGui::TextDisabled("%s", formatPlaytime(g.playtimeSec).c_str());

    ImGui::PopID();
    ImGui::EndGroup();
}

bool EditorApp::libNavA() const {
    // A-launch guard: ignore presses for the first couple of frames after the
    // cursor moves, so holding D-pad and tapping A can't fire while the cursor
    // is still travelling between targets. No auto-repeat: holding A must not
    // relaunch a game or cycle a filter repeatedly.
    if (ImGui::GetFrameCount() - libNavMoveFrame < 2) return false;
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) return false;
    return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false);
}

void EditorApp::drawScanDialog() {
    if (!scanDialogOpen) return;
    ImGui::OpenPopup("Scanning folder");
    if (!ImGui::BeginPopupModal("Scanning folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    advanceScan();
    float frac = scanTotal ? (float)scanDone / (float)scanTotal : 1.0f;
    char buf[96];
    snprintf(buf, sizeof(buf), "%d / %d", (int)scanDone, (int)scanTotal);
    ImGui::ProgressBar(frac, ImVec2(360, 0), buf);
    ImGui::TextDisabled("Added %d, skipped %d (already in library)", (int)scanAdded,
                        (int)scanSkipped);
    if (scanDone < scanTotal) {
        if (ImGui::Button("Cancel")) {
            scanQueue.clear();
            scanDialogOpen = false;
            libStatus = "Scan cancelled.";
            libStatusErr.clear();
        }
    }
    ImGui::EndPopup();
}

void EditorApp::drawImportDialog() {
    if (!importDialogOpen) return;
    ImGui::OpenPopup("Import by Title ID");
    if (!ImGui::BeginPopupModal("Import by Title ID", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    std::vector<CompatEntry> all = compat.all();
    if (all.empty()) {
        if (compat.busy)
            ImGui::TextDisabled("Loading compatibility data...");
        else
            ImGui::TextDisabled("No compatibility data available yet.");
        if (ImGui::Button("Close")) {
            importDialogOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }
    std::stable_sort(all.begin(), all.end(), [](const CompatEntry& a, const CompatEntry& b) {
        std::string x = a.title, y = b.title;
        for (auto& c : x) c = (char)tolower((unsigned char)c);
        for (auto& c : y) c = (char)tolower((unsigned char)c);
        return x < y;
    });
    ImGui::SetNextItemWidth(380.0f);
    ImGui::InputText("##importsearch", &importSearch);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) importSearch.clear();
    ImGui::Separator();
    ImGui::BeginChild("importlist", ImVec2(420, 300), true);
    int shownCount = 0;
    for (auto& ce : all) {
        if (!importSearch.empty() && !containsIgnoreCase(ce.title, importSearch) &&
            !containsIgnoreCase(ce.id, importSearch))
            continue;
        shownCount++;
        bool sel = importSelId == ce.id;
        if (ImGui::Selectable(ce.title.c_str(), sel)) importSelId = ce.id;
        if (sel) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s - %s", ce.id.c_str(), ce.state.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%d titles", shownCount);
    ImGui::Separator();
    ImGui::BeginDisabled(importSelId.empty());
    if (ImGui::Button("Add to Library")) importByTitleId();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) importDialogOpen = false;
    ImGui::EndPopup();
}

void EditorApp::drawAddDialog() {
    if (!addDialogOpen) return;
    ImGui::OpenPopup("Add game to library");
    if (!ImGui::BeginPopupModal("Add game to library", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Path:");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", addPath.c_str());
    ImGui::TextUnformatted("Type:");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", addType.c_str());

    ImGui::TextUnformatted("Title (editable):");
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("##addname", addNameBuf, sizeof(addNameBuf));
    ImGui::TextUnformatted("Title ID (8 hex chars, used for compatibility + per-game config):");
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("##addtitle", addTitleBuf, sizeof(addTitleBuf));

    ImGui::Separator();

    ID3D11ShaderResourceView* srv = tex.get(addCover);
    if (srv) {
        ImGui::Image(srv, ImVec2(120, 168));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    if (ImGui::Button("Choose Cover Image...")) {
        std::filesystem::path p;
        if (pickImageFile(p)) addCover = p.string();
    }
    if (ImGui::Button("Download Cover")) downloadCoverForAdd();
    if (ImGui::Button("Clear Cover")) addCover.clear();
    ImGui::EndGroup();

    ImGui::Separator();

    bool ok = strlen(addNameBuf) > 0;
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button("Add to Library")) {
        finishAddGame();
        if (!addDialogOpen) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        addQueue.clear();
        addDialogOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorApp::drawEditDialog() {
    if (!editDialogOpen) return;
    ImGui::OpenPopup("Edit game");
    if (!ImGui::BeginPopupModal("Edit game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Title:");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##editname", editNameBuf, sizeof(editNameBuf));
    ImGui::TextUnformatted("Title ID:");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##edittitle", editTitleBuf, sizeof(editTitleBuf));
    ImGui::TextUnformatted("Game file (path, can be empty for a wishlist entry):");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##editpath", editPathBuf, sizeof(editPathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::filesystem::path p;
        if (pickOneGameFile(p))
            snprintf(editPathBuf, sizeof(editPathBuf), "%s", p.string().c_str());
    }
    ImGui::TextUnformatted("Launch arguments (appended to xenia, e.g. --vsync=false):");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##editargs", editArgsBuf, sizeof(editArgsBuf));
    ImGui::Separator();
    bool ok = strlen(editNameBuf) > 0;
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button("Save")) {
        saveEditGame();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        editDialogOpen = false;
        editGameIdx = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::drawRemoveConfirm() {
    if (confirmRemoveIdx < 0) return;
    ImGui::OpenPopup("Remove game");
    if (!ImGui::BeginPopupModal("Remove game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Remove this game from your library?");
    if (confirmRemoveIdx >= 0 && confirmRemoveIdx < (int)library.games.size())
        ImGui::TextDisabled("%s", library.games[confirmRemoveIdx].name.c_str());
    ImGui::Separator();
    if (ImGui::Button("Remove")) {
        removeGame(confirmRemoveIdx);
        confirmRemoveIdx = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        confirmRemoveIdx = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::drawCompat() {
    if (ImGui::Button("Refresh compat data")) compat.startFetch();
    ImGui::SameLine();
    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("##compatsearch", &compatSearch);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) compatSearch.clear();
    ImGui::SameLine();
    if (compat.busy)
        ImGui::TextDisabled("Updating compatibility data...");
    else if (compat.loaded)
        ImGui::TextDisabled("%d titles (%s)", (int)compat.size(), compat.source.c_str());
    else if (!compat.lastError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Failed to load: %s",
                           compat.lastError.c_str());
    else
        ImGui::TextDisabled("Compatibility data not loaded");

    ImGui::Separator();

    std::vector<CompatEntry> all = compat.all();
    if (all.empty()) {
        if (compat.busy)
            ImGui::TextDisabled("Fetching compatibility data...");
        else
            ImGui::TextDisabled("No compatibility data available.");
        return;
    }

    std::stable_sort(all.begin(), all.end(), [](const CompatEntry& a, const CompatEntry& b) {
        std::string x = a.title, y = b.title;
        for (auto& c : x) c = (char)tolower((unsigned char)c);
        for (auto& c : y) c = (char)tolower((unsigned char)c);
        return x < y;
    });

    if (ImGui::BeginTable("compat", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Title ID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Updated", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        int shownCount = 0;
        for (const auto& ce : all) {
            if (!compatSearch.empty() &&
                !containsIgnoreCase(ce.title, compatSearch) &&
                !containsIgnoreCase(ce.id, compatSearch))
                continue;
            shownCount++;
            ImGui::TableNextRow();
            bool sel = compatSelId == ce.id;
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(ce.title.c_str(), sel,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                compatSelId = ce.id;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetClipboardText(ce.id.c_str());
                    compatSelId = ce.id;
                    libStatus = "Copied title ID " + ce.id + " to clipboard";
                    libStatusErr.clear();
                }
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", ce.id.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::PushStyleColor(ImGuiCol_Text, compatColor(ce.state));
            ImGui::TextUnformatted(ce.state.c_str());
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", ce.updated.c_str());
        }
        ImGui::EndTable();

        ImGui::TextDisabled("Showing %d of %d titles - double-click a row to copy its title ID",
                            shownCount, (int)all.size());
    }
}

std::string EditorApp::targetLabel() const {
    if (openFiles.empty()) return "no config loaded";
    auto& fe = openFiles[selectedFile];
    if (fe.path.empty()) return "no config loaded";
    return fe.label + "  [" + fe.path.string() + "]";
}
