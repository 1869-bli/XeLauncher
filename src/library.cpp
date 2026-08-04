#include "library.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "json.h"
#include "net.h"

namespace {

const char* kLaunchBoxUrl =
    "https://raw.githubusercontent.com/xenia-manager/database/refs/heads/main/data/metadata/"
    "launchbox/games.json";

std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string toUpper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool writeFile(const std::filesystem::path& p, const std::string& s) {
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(s.data(), (std::streamsize)s.size());
    return (bool)ofs;
}

std::wstring utf8ToWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, 0);
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

// Big-endian 32-bit read.
unsigned long be32(const unsigned char* b) {
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16) | ((unsigned long)b[2] << 8) |
           (unsigned long)b[3];
}

std::string formatTitleId(unsigned long id) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%08X", id);
    return buf;
}

bool isValidTitleId(const std::string& id) {
    if (id.size() != 8) return false;
    for (char c : id)
        if (!isxdigit((unsigned char)c)) return false;
    if (id == "00000000" || id == "FFFFFFFF" || id == "DEADBEEF") return false;
    return true;
}

// Reads up to n bytes from the file into buf; returns bytes read.
size_t readAt(const std::filesystem::path& p, long long offset, unsigned char* buf, size_t n) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return 0;
    in.seekg((std::streamoff)offset, std::ios::beg);
    if (!in) return 0;
    in.read((char*)buf, (std::streamsize)n);
    return (size_t)in.gcount();
}

constexpr size_t kXexHeaderSize = 0x200;

// Returns the title id from the first 512 bytes if it is a XEX2 executable.
std::string titleIdFromXexBytes(const unsigned char* h) {
    if (memcmp(h, "XEX2", 4) != 0) return "";
    if (be32(h + 0x08) < 0x180) return "";  // implausible header size
    unsigned long id = be32(h + 0x14);
    return formatTitleId(id);
}

std::string titleIdFromFile(const std::filesystem::path& p) {
    unsigned char h[kXexHeaderSize];
    size_t n = readAt(p, 0, h, sizeof(h));
    if (n < 0x180) return "";
    return titleIdFromXexBytes(h);
}

// Scans the first ~8MB of an ISO for an XEX2 header (the system area's
// default.xex). Returns the first plausible title id found.
std::string titleIdFromIso(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return "";
    unsigned char buf[2048];
    long long limit = 8LL * 1024 * 1024;
    for (long long off = 0; off < limit; off += (long long)sizeof(buf)) {
        in.seekg((std::streamoff)off, std::ios::beg);
        if (!in) break;
        in.read((char*)buf, (std::streamsize)sizeof(buf));
        size_t got = (size_t)in.gcount();
        if (got < 0x180) break;
        for (size_t i = 0; i + 0x18 <= got; i += 2) {
            if (buf[i] == 'X' && buf[i + 1] == 'E' && buf[i + 2] == 'X' && buf[i + 3] == '2') {
                std::string id = titleIdFromXexBytes(buf + i);
                if (isValidTitleId(id)) return id;
            }
        }
        if (got < sizeof(buf)) break;
    }
    return "";
}

std::string titleIdFromFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    std::vector<std::filesystem::path> xexs;
    for (auto& de : std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!de.is_regular_file(ec)) continue;
        std::string ext = toUpper(de.path().extension().string());
        if (ext == ".XEX") {
            xexs.push_back(de.path());
            if (de.path().filename().string() == "default.xex") break;
        }
        if (xexs.size() >= 8) break;
    }
    for (auto& x : xexs) {
        std::string id = titleIdFromFile(x);
        if (isValidTitleId(id)) return id;
    }
    return "";
}

}  // namespace

std::string detectTitleIdFromFile(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return "";
    if (std::filesystem::is_directory(p, ec)) return titleIdFromFolder(p);
    std::string ext = toUpper(p.extension().string());
    if (ext == ".XEX" || ext == ".XBE" || ext == ".ZAR") {
        std::string id = titleIdFromFile(p);
        return isValidTitleId(id) ? id : "";
    }
    if (ext == ".ISO" || ext == ".GOD") return titleIdFromIso(p);
    // Unknown extension: try a direct XEX read first, then a light scan.
    std::string id = titleIdFromFile(p);
    if (isValidTitleId(id)) return id;
    return "";
}

std::string guessGameName(const std::string& path) {
    std::string fn = std::filesystem::path(path).filename().string();
    size_t dot = fn.find_last_of('.');
    if (dot != std::string::npos) fn = fn.substr(0, dot);
    // Drop parenthesized/bracketed tags like "(USA)" or "[Region Free]".
    std::string noTags;
    int depth = 0;
    for (char c : fn) {
        if (c == '(' || c == '[' || c == '{') depth++;
        if (depth > 0) continue;
        if (c == ')' || c == ']' || c == '}') continue;
        noTags.push_back(c);
    }
    std::string out;
    bool lastSpace = false;
    for (char c : noTags) {
        if (c == '_' || c == '-') c = ' ';
        if (c == ' ') {
            if (!lastSpace) {
                out.push_back(' ');
                lastSpace = true;
            }
        } else {
            out.push_back(c);
            lastSpace = false;
        }
    }
    return trim(out);
}

bool Library::load() {
    games.clear();
    auto p = dataDir / "library.json";
    std::string text = readFile(p);
    if (text.empty()) return false;
    JsonValue root = JsonValue::parse(text);
    const JsonValue* arr = root.get("games");
    if (!arr || !arr->isArray()) return false;
    for (const auto& e : arr->arr) {
        if (!e.isObject()) continue;
        GameEntry g;
        if (const JsonValue* v = e.get("name")) g.name = v->asString();
        if (const JsonValue* v = e.get("titleId")) g.titleId = v->asString();
        if (const JsonValue* v = e.get("path")) g.path = v->asString();
        if (const JsonValue* v = e.get("type")) g.type = v->asString();
        if (const JsonValue* v = e.get("cover")) g.cover = v->asString();
        if (const JsonValue* v = e.get("lastPlayed")) g.lastPlayed = (long long)v->asNumber();
        if (const JsonValue* v = e.get("launches")) g.launches = (int)v->asNumber();
        if (const JsonValue* v = e.get("playtime")) g.playtimeSec = (long long)v->asNumber();
        if (const JsonValue* v = e.get("fav")) g.fav = v->asBool();
        games.push_back(std::move(g));
    }
    return true;
}

bool Library::save() {
    JsonValue root = JsonValue::makeObject();
    JsonValue arr = JsonValue::makeArray();
    for (const auto& g : games) {
        JsonValue o = JsonValue::makeObject();
        o.obj.emplace_back("name", JsonValue::makeString(g.name));
        o.obj.emplace_back("titleId", JsonValue::makeString(g.titleId));
        o.obj.emplace_back("path", JsonValue::makeString(g.path));
        o.obj.emplace_back("type", JsonValue::makeString(g.type));
        o.obj.emplace_back("cover", JsonValue::makeString(g.cover));
        o.obj.emplace_back("lastPlayed", JsonValue::makeNumber((double)g.lastPlayed));
        o.obj.emplace_back("launches", JsonValue::makeNumber(g.launches));
        o.obj.emplace_back("playtime", JsonValue::makeNumber((double)g.playtimeSec));
        o.obj.emplace_back("fav", JsonValue::makeBool(g.fav));
        arr.arr.push_back(std::move(o));
    }
    root.obj.emplace_back("games", std::move(arr));
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    return writeFile(dataDir / "library.json", root.serialize(1));
}

GameEntry* Library::find(const std::string& path) {
    for (auto& g : games)
        if (g.path == path) return &g;
    return nullptr;
}

int Library::add(const GameEntry& e) {
    games.push_back(e);
    return (int)games.size() - 1;
}

bool Library::remove(int idx) {
    if (idx < 0 || idx >= (int)games.size()) return false;
    games.erase(games.begin() + idx);
    return true;
}

bool lookupCoverUrl(const std::filesystem::path& dataDir, const std::string& name,
                    std::string& urlOut) {
    if (name.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    auto cache = dataDir / "launchbox.json";

    std::string text;
    if (std::filesystem::exists(cache, ec)) {
        text = readFile(cache);
    }
    if (text.empty()) {
        std::string tmp;
        if (!httpGet(utf8ToWide(kLaunchBoxUrl), tmp)) return false;
        if (tmp.empty()) return false;
        writeFile(cache, tmp);
        text = std::move(tmp);
    }

    JsonValue root = JsonValue::parse(text);
    if (!root.isArray()) return false;

    std::string upper = toUpper(name);
    for (const auto& e : root.arr) {
        if (!e.isObject()) continue;
        const JsonValue* n = e.get("Name");
        if (!n) continue;
        if (toUpper(n->asString()) != upper) continue;
        const JsonValue* art = e.get("Artwork");
        if (!art || !art->isObject()) continue;
        const JsonValue* front = nullptr;
        if (const JsonValue* box = art->get("Box")) {
            if (box->isObject()) front = box->get("Front");
        }
        if (!front || !front->isArray()) front = art->get("Box Front");
        if (!front || !front->isArray()) continue;
        const JsonValue* best = nullptr;
        for (const auto& item : front->arr) {
            if (!item.isObject()) continue;
            const JsonValue* r = item.get("Region");
            std::string region = r ? toUpper(r->asString()) : "";
            if (!best) best = &item;
            if (region.find("UNITED KINGDOM") != std::string::npos ||
                region.find("EU") != std::string::npos ||
                region.find("PAL") != std::string::npos ||
                region.find("NORTH AMERICA") != std::string::npos ||
                region.find("UNITED STATES") != std::string::npos ||
                region.find("WORLD") != std::string::npos ||
                region.find("USA") != std::string::npos) {
                best = &item;
                break;
            }
        }
        if (!best) continue;
        const JsonValue* url = best->get("URL");
        if (url && !url->asString().empty()) {
            urlOut = url->asString();
            return true;
        }
    }
    return false;
}

bool fetchLaunchBoxCache(const std::filesystem::path& dataDir) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    auto cache = dataDir / "launchbox.json";
    if (std::filesystem::exists(cache, ec)) return true;
    std::string tmp;
    if (!httpGet(utf8ToWide(kLaunchBoxUrl), tmp)) return false;
    if (tmp.empty()) return false;
    return writeFile(cache, tmp);
}
