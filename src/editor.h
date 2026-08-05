#pragma once
#include "config.h"
#include "compat.h"
#include "library.h"
#include "texture.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

struct ID3D11Device;

struct EnumOption {
    std::string value;
    std::string label;
    std::string desc;
};

struct SettingInfo {
    std::string full;
    std::string category;
    std::string key;
    ValType type = ValType::Raw;
    bool isEnum = false;
    std::vector<EnumOption> enums;
    std::string description;
    bool isPath = false;
    bool pathIsFolder = false;
    bool unsafe = false;
    bool present = false;
    std::string effRaw;
    std::string baseRaw;
    std::string defaultRaw;
    bool modified = false;
    bool favorite = false;
    char buf[1024];
    long long iVal = 0;
    double fVal = 0.0;
};

struct FileEntry {
    std::string label;
    std::filesystem::path path;
};

std::filesystem::path editorDataDir();

struct EditorApp {
    ConfigFile defaults;
    ConfigFile global;
    ConfigFile target;
    std::filesystem::path xeniaDir;
    std::filesystem::path defaultsPath;
    std::vector<SettingInfo> settings;
    std::vector<std::string> categories;
    std::set<std::string> favorites;
    std::vector<FileEntry> openFiles;
    int selectedFile = 0;
    bool editingPerGame = false;
    std::string search;
    std::string selectedCategory;
    bool onlyChanged = false;
    bool onlyFavorites = false;
    bool onlyUnsafe = false;
    char gameIdBuf[32] = {};
    char rawBuf[1024] = {};
    std::string rawEditTarget;
    bool focusSearch = false;
    bool wantSave = false;
    bool wantReload = false;
    bool wantNewPerGame = false;
    bool newPerGameOpen = false;
    bool confirmResetAll = false;
    bool showAbout = false;
    std::string statusMsg;
    bool settingsDirty = false;

    // Launcher / library
    ID3D11Device* device = nullptr;
    TextureCache tex;
    Library library;
    CompatDB compat;
    int tab = 0; // 0 = Library, 1 = Config Editor, 2 = Compatibility
    std::string libSearch;
    char libSearchBuf[96] = {};
    std::string compatSearch;
    std::string compatSelId;
    int selectedGame = -1;
    int confirmRemoveIdx = -1;
    int editGameIdx = -1;
    char editNameBuf[256] = {};
    char editTitleBuf[32] = {};
    char editPathBuf[1024] = {};
    char editArgsBuf[512] = {};
    bool editDialogOpen = false;
    std::string libStatus;
    std::string libStatusErr;

    // Library view options
    bool libListView = false;
    bool libOnlyFavorites = false;
    int libSort = 0;          // 0 name A-Z, 1 name Z-A, 2 recent, 3 most played, 4 playtime, 5 favs first
    int libCompatFilter = 0;  // 0 any, 1 playable, 2 gameplay, 3 loads, 4 unplayable, 5 no data

    // Playtime tracking
    void* runningProc = nullptr;  // HANDLE to the running xenia process
    int runningIdx = -1;
    long long runningStartMs = 0;
    long long lastLaunchMs = -100000;  // steady-clock ms of last game launch (debounce)
    void* hwnd = nullptr;              // launcher window handle, set by main

    // Folder scan
    std::vector<std::filesystem::path> scanQueue;
    size_t scanDone = 0, scanTotal = 0, scanAdded = 0, scanSkipped = 0;
    bool scanDialogOpen = false;

    // Import by Title ID
    bool importDialogOpen = false;
    std::string importSearch;
    std::string importSelId;

    // Add-game dialog state
    std::vector<std::filesystem::path> addQueue;
    bool addDialogOpen = false;
    std::string addPath;
    std::string addType;
    char addNameBuf[256] = {};
    char addTitleBuf[32] = {};
    std::string addCover;
    std::atomic<bool> launchboxFetching{false};
    std::thread launchboxThread;
    void ensureLaunchBoxCached();
    void shutdown();

    void init();
    void rebuild();
    void refreshSettingValue(SettingInfo& s);
    void applyEdit(SettingInfo& s, const std::string& raw);
    void saveTarget();
    void switchFile(int idx);
    void browseOpenFile();
    void createPerGame(const std::string& titleId);
    void exportTargetAs();
    void loadFavorites();
    void saveFavorites();
    void loadAppSettings();
    void saveAppSettings();
    void setXeniaDir(const std::filesystem::path& p);
    void scanPerGameFiles();
    void resetSetting(SettingInfo& s);
    void removeOverride(SettingInfo& s);
    void toggleFavorite(const std::string& full);
    void drawRow(SettingInfo& s);
    void drawMenuBar();
    void draw();
    void drawLibrary();
    void drawConfigEditor();
    void drawCompat();
    void drawGameTile(int idx, float w);
    void drawGameListRow(int idx);
    void drawDetailsPanel();
    void gameContextMenu(int idx);
    void toggleGameFav(int idx);
    void drawAddDialog();
    void drawEditDialog();
    void drawRemoveConfirm();
    void drawScanDialog();
    void drawImportDialog();
    void beginAddGameFiles();
    void beginAddGameFolder();
    void beginFolderScan();
    void advanceScan();
    void advanceAddQueue();
    void finishAddGame();
    void launchGame(int idx);
    void monitorRunning();
    void createDesktopShortcut(int idx);
    void openImportDialog();
    void importByTitleId();
    void pickCoverFor(int idx);
    void downloadCoverFor(int idx);
    void downloadCoverTo(const std::string& name, const std::string& titleId, std::string& out);
    void downloadCoverForAdd();
    void openEditGame(int idx);
    void saveEditGame();
    void removeGame(int idx);
    void openPerGameConfig(int idx);
    std::string targetLabel() const;
};
