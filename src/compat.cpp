#include "compat.h"

#include <thread>

#include <cctype>
#include <cstdio>
#include <fstream>

#include "json.h"
#include "net.h"

namespace {

const wchar_t* kCompatUrls[] = {
    L"https://xenia-manager.github.io/database/data/game-compatibility/canary.json",
    L"https://raw.githubusercontent.com/xenia-manager/database/refs/heads/main/data/"
    L"game-compatibility/canary.json",
};

bool isValidId(const std::string& id) {
    if (id.size() != 8) return false;
    for (char c : id) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return id != "00000000" && id != "FFFFFFFF" && id != "DEADBEEF";
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

}  // namespace

bool CompatDB::loadCache() {
    std::string text = readFile(cachePath);
    if (text.empty()) return false;
    JsonValue root = JsonValue::parse(text);
    if (!root.isArray()) return false;
    std::map<std::string, CompatEntry> fresh;
    for (const auto& e : root.arr) {
        if (!e.isObject()) continue;
        const JsonValue* id = e.get("id");
        if (!id || !isValidId(id->asString())) continue;
        CompatEntry ce;
        ce.id = id->asString();
        if (const JsonValue* v = e.get("title")) ce.title = v->asString();
        if (const JsonValue* v = e.get("state")) ce.state = v->asString();
        if (const JsonValue* v = e.get("updated")) ce.updated = v->asString();
        if (const JsonValue* v = e.get("url")) ce.url = v->asString();
        fresh[ce.id] = std::move(ce);
    }
    if (fresh.empty()) return false;
    {
        std::lock_guard<std::mutex> lk(mtx);
        byId = std::move(fresh);
        loaded = true;
        source = "cached";
    }
    return true;
}

void CompatDB::saveCache() {
    std::vector<CompatEntry> copy;
    {
        std::lock_guard<std::mutex> lk(mtx);
        for (const auto& kv : byId) copy.push_back(kv.second);
    }
    JsonValue arr = JsonValue::makeArray();
    for (const auto& ce : copy) {
        JsonValue o = JsonValue::makeObject();
        o.obj.emplace_back("id", JsonValue::makeString(ce.id));
        o.obj.emplace_back("title", JsonValue::makeString(ce.title));
        o.obj.emplace_back("state", JsonValue::makeString(ce.state));
        o.obj.emplace_back("updated", JsonValue::makeString(ce.updated));
        o.obj.emplace_back("url", JsonValue::makeString(ce.url));
        arr.arr.push_back(std::move(o));
    }
    writeFile(cachePath, arr.serialize(-1));
}

void CompatDB::fetchAndSave() {
    std::string text;
    bool ok = false;
    for (const wchar_t* url : kCompatUrls) {
        if (httpGet(url, text, 20000)) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        lastError = "network fetch failed";
        busy = false;
        return;
    }
    JsonValue root = JsonValue::parse(text);
    if (!root.isArray()) {
        lastError = "bad response format";
        busy = false;
        return;
    }
    std::map<std::string, CompatEntry> fresh;
    for (const auto& e : root.arr) {
        if (!e.isObject()) continue;
        const JsonValue* id = e.get("id");
        if (!id || !isValidId(id->asString())) continue;
        CompatEntry ce;
        ce.id = id->asString();
        if (const JsonValue* v = e.get("title")) ce.title = v->asString();
        if (const JsonValue* v = e.get("state")) ce.state = v->asString();
        if (const JsonValue* v = e.get("updated")) ce.updated = v->asString();
        if (const JsonValue* v = e.get("url")) ce.url = v->asString();
        fresh[ce.id] = std::move(ce);
    }
    if (fresh.empty()) {
        lastError = "no entries parsed";
        busy = false;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mtx);
        byId = std::move(fresh);
        loaded = true;
        source = "online";
    }
    lastError.clear();
    saveCache();
    busy = false;
}

void CompatDB::startFetch() {
    if (busy.exchange(true)) return;
    if (thread.joinable()) thread.join();
    thread = std::thread([this]() { fetchAndSave(); });
}

void CompatDB::shutdown() {
    if (thread.joinable()) thread.join();
}

CompatEntry CompatDB::find(const std::string& titleId) const {
    if (titleId.empty()) return CompatEntry();
    std::string upper = titleId;
    for (auto& c : upper) c = (char)toupper((unsigned char)c);
    std::lock_guard<std::mutex> lk(mtx);
    auto it = byId.find(upper);
    return it == byId.end() ? CompatEntry() : it->second;
}

size_t CompatDB::size() const {
    std::lock_guard<std::mutex> lk(mtx);
    return byId.size();
}

std::vector<CompatEntry> CompatDB::all() const {
    std::vector<CompatEntry> out;
    std::lock_guard<std::mutex> lk(mtx);
    out.reserve(byId.size());
    for (const auto& kv : byId) out.push_back(kv.second);
    return out;
}
