#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

std::string trimStr(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string decodeStr(const std::string& raw) {
    if (raw.size() < 2) return raw;
    if (raw[0] == '"' || raw[0] == '\'') {
        std::string out;
        size_t i = 1;
        if (raw.size() >= 3 && raw[0] == raw[1] && raw[1] == raw[2]) i = 3;
        size_t n = raw.size();
        while (i < n) {
            char c = raw[i];
            if (c == raw[0] && (c == '"' || c == '\'')) {
                if (raw[0] == '"' && i + 2 < n && raw[i + 1] == '"' && raw[i + 2] == '"') break;
                if (i + 1 < n && raw[i] == raw[i + 1]) { i++; continue; }
                break;
            }
            if (raw[0] == '"' && c == '\\' && i + 1 < n) {
                char nxt = raw[i + 1];
                switch (nxt) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'u': {
                        if (i + 6 <= n) {
                            unsigned int cp = (unsigned int)strtoul(raw.substr(i + 2, 4).c_str(), nullptr, 16);
                            if (cp == 0) break;
                            if (cp < 0x80) out += (char)cp;
                            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                            else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                            i += 5;
                        }
                        break;
                    }
                    default: out += nxt; break;
                }
                i += 2;
                continue;
            }
            out += c;
            i++;
        }
        return out;
    }
    return raw;
}

std::string encodeStr(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    out += "\"";
    return out;
}

bool looksLikeInt(const std::string& raw) {
    std::string s = trimStr(raw);
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    if (i >= s.size()) return false;
    if (s[i] == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'o' || s[i + 1] == 'b')) {
        char baseChar = s[i + 1];
        for (size_t j = i + 2; j < s.size(); j++) {
            char c = s[j];
            if (c == '_') continue;
            bool ok = baseChar == 'x' ? (isxdigit((unsigned char)c) != 0)
                                      : (c >= '0' && c <= (baseChar == 'b' ? '1' : '7'));
            if (!ok) return false;
        }
        return true;
    }
    for (size_t j = i; j < s.size(); j++) {
        char c = s[j];
        if (c == '_') continue;
        if (!isdigit((unsigned char)c)) return false;
    }
    return true;
}

bool looksLikeFloat(const std::string& raw) {
    std::string s = trimStr(raw);
    if (s.empty()) return false;
    if (s == "inf" || s == "+inf" || s == "-inf" || s == "nan" || s == "+nan" || s == "-nan") return true;
    bool hasDigit = false, hasDot = false, hasExp = false;
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (c == '_') continue;
        if (isdigit((unsigned char)c)) { hasDigit = true; }
        else if (c == '.') hasDot = true;
        else if (c == 'e' || c == 'E') {
            hasExp = true;
            if (i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-')) i++;
        }
        else return false;
    }
    return hasDigit && (hasDot || hasExp);
}

std::string normalizeInt(const std::string& v) {
    std::string s = trimStr(v);
    try {
        long long val = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            val = std::stoll(s, nullptr, 16);
        else if (s.size() > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
            val = std::stoll(s, nullptr, 8);
        else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
            val = std::stoll(s.substr(2), nullptr, 2);
        else
            val = std::stoll(s, nullptr, 10);
        return std::to_string(val);
    } catch (...) {
        return v;
    }
}

std::string normalizeFloat(const std::string& v) {
    std::string s = trimStr(v);
    if (s.empty()) return v;
    if (s == "inf") return "inf";
    if (s == "-inf") return "-inf";
    if (s == "nan" || s == "+nan") return "nan";
    try {
        double d = std::stod(s);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.9g", d);
        return buf;
    } catch (...) {
        return v;
    }
}

static std::string scanValue(const std::vector<std::string>& lines, int startLine,
                             int startCol, int& outEndLine, int& outEndCol) {
    int li = startLine;
    int col = startCol;
    if (li >= (int)lines.size() || col >= (int)lines[li].size()) {
        outEndLine = li;
        outEndCol = col;
        return "";
    }
    char c = lines[li][col];
    if (c == '"' || c == '\'') {
        char q = c;
        bool multiline = false;
        std::string rest = lines[li].substr(col);
        if (rest.size() >= 3 && rest[0] == q && rest[1] == q && rest[2] == q) {
            multiline = true;
            col += 3;
        } else {
            col += 1;
        }
        bool closed = false;
        while (true) {
            if (li >= (int)lines.size()) break;
            const std::string& L = lines[li];
            while (col < (int)L.size()) {
                char ch = L[col];
                if (q == '"' && ch == '\\' && col + 1 < (int)L.size()) {
                    col += 2;
                    continue;
                }
                if (ch == q) {
                    if (multiline) {
                        if (col + 2 < (int)L.size() && L[col + 1] == q && L[col + 2] == q) {
                            col += 3;
                            closed = true;
                            break;
                        }
                        col++;
                        continue;
                    }
                    col += 1;
                    closed = true;
                    break;
                }
                col++;
            }
            if (closed) break;
            li++;
            col = 0;
        }
        outEndLine = li;
        outEndCol = col;
        std::string token;
        for (int l = startLine; l <= li; l++) {
            int from = (l == startLine) ? startCol : 0;
            int to = (l == li) ? col : (int)lines[l].size();
            if (!token.empty()) token += '\n';
            token += lines[l].substr(from, to - from);
        }
        return token;
    }
    if (c == '[' || c == '{') {
        int depth = 0;
        bool inStr = false;
        char sq = 0;
        bool closed = false;
        while (true) {
            if (li >= (int)lines.size()) break;
            const std::string& L = lines[li];
            while (col < (int)L.size()) {
                char ch = L[col];
                if (inStr) {
                    if (sq == '"' && ch == '\\') { col += 2; continue; }
                    if (ch == sq) inStr = false;
                } else if (ch == '"' || ch == '\'') {
                    inStr = true;
                    sq = ch;
                } else if (ch == '[' || ch == '{') {
                    depth++;
                } else if (ch == ']' || ch == '}') {
                    depth--;
                    if (depth == 0) {
                        col++;
                        closed = true;
                        break;
                    }
                }
                col++;
            }
            if (closed) break;
            li++;
            col = 0;
        }
        outEndLine = li;
        outEndCol = col;
        std::string token;
        for (int l = startLine; l <= li; l++) {
            int from = (l == startLine) ? startCol : 0;
            int to = (l == li) ? col : (int)lines[l].size();
            if (!token.empty()) token += '\n';
            token += lines[l].substr(from, to - from);
        }
        return token;
    }
    const std::string& L = lines[li];
    int e2 = col;
    while (e2 < (int)L.size()) {
        char ch = L[e2];
        if (ch == ' ' || ch == '\t' || ch == '#') break;
        e2++;
    }
    outEndLine = li;
    outEndCol = e2;
    return L.substr(col, e2 - col);
}

static ValType parseValueType(const std::string& raw) {
    if (raw == "true" || raw == "false") return ValType::Bool;
    if (!raw.empty() && (raw[0] == '"' || raw[0] == '\'')) return ValType::Str;
    if (!raw.empty() && (raw[0] == '[' || raw[0] == '{')) return ValType::Arr;
    if (looksLikeInt(raw)) return ValType::Int;
    if (looksLikeFloat(raw)) return ValType::Float;
    return ValType::Raw;
}

bool ConfigFile::load(const std::filesystem::path& p) {
    sections.clear();
    entries.clear();
    lines.clear();
    loaded = false;
    error.clear();
    path = p;

    std::ifstream in(p, std::ios::binary);
    if (!in) {
        error = "Cannot open " + p.string();
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool bom = content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
               (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF;
    if (bom) content = content.substr(3);
    crlf = content.find("\r\n") != std::string::npos;

    std::string cur;
    for (size_t i = 0; i < content.size(); i++) {
        char ch = content[i];
        if (ch == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) lines.push_back(cur);

    ConfigSection* curSec = nullptr;
    std::vector<std::string> pendingTop;
    int i = 0;
    int n = (int)lines.size();
    while (i < n) {
        std::string t = trimStr(lines[i]);
        if (t.empty()) {
            pendingTop.clear();
            i++;
            continue;
        }
        if (t[0] == '#') {
            pendingTop.push_back(t.substr(1));
            i++;
            continue;
        }
        if (t[0] == '[') {
            size_t close = t.find(']');
            std::string name = close == std::string::npos ? t.substr(1) : t.substr(1, close - 1);
            ConfigSection sec;
            sec.name = name;
            sections.push_back(sec);
            curSec = &sections.back();
            pendingTop.clear();
            i++;
            continue;
        }
        if (!curSec) {
            ConfigSection sec;
            sec.name = "";
            sections.push_back(sec);
            curSec = &sections.back();
        }
        size_t eq = lines[i].find('=');
        if (eq == std::string::npos) {
            i++;
            continue;
        }
        std::string key = trimStr(lines[i].substr(0, eq));
        size_t after = eq + 1;
        size_t vs = lines[i].find_first_not_of(" \t", after);
        if (vs == std::string::npos) {
            i++;
            continue;
        }
        int endLine = i;
        int endCol = (int)vs;
        std::string valText = scanValue(lines, i, (int)vs, endLine, endCol);

        auto e = std::make_unique<ConfigEntry>();
        e->category = curSec->name;
        e->key = key;
        e->line = i;
        e->endLine = endLine;
        e->valueStart = (int)vs;
        e->valueEnd = endCol;
        e->raw = valText;
        e->type = parseValueType(valText);

        if (!pendingTop.empty()) {
            e->desc.insert(e->desc.end(), pendingTop.begin(), pendingTop.end());
            pendingTop.clear();
        }
        std::string rest = lines[i].substr(endCol);
        size_t hash = rest.find('#');
        if (hash != std::string::npos) {
            e->desc.push_back(trimStr(rest.substr(hash + 1)));
        }
        int j = endLine + 1;
        while (j < n) {
            std::string tj = trimStr(lines[j]);
            if (tj.empty()) break;
            if (tj[0] != '#') break;
            e->desc.push_back(trimStr(tj.substr(1)));
            j++;
        }
        curSec->entries.push_back(e.get());
        entries.push_back(std::move(e));
        i = j;
    }

    loaded = true;
    return true;
}

ConfigEntry* ConfigFile::find(const std::string& category, const std::string& key) {
    for (auto& e : entries)
        if (e->category == category && e->key == key) return e.get();
    return nullptr;
}

ConfigEntry* ConfigFile::set(const std::string& category, const std::string& key,
                             ValType type, const std::string& raw,
                             const std::vector<std::string>& desc) {
    ConfigEntry* e = find(category, key);
    if (e) {
        if (e->raw != raw) {
            e->raw = raw;
            e->dirty = true;
            e->deleted = false;
        }
        return e;
    }
    auto ne = std::make_unique<ConfigEntry>();
    ne->category = category;
    ne->key = key;
    ne->type = type;
    ne->raw = raw;
    ne->desc = desc;
    ne->line = -1;
    ne->added = true;
    ne->dirty = true;
    ConfigEntry* ret = ne.get();
    ConfigSection* sec = nullptr;
    for (auto& s : sections)
        if (s.name == category) sec = &s;
    if (!sec) {
        ConfigSection ns;
        ns.name = category;
        sections.push_back(ns);
        sec = &sections.back();
    }
    sec->entries.push_back(ret);
    entries.push_back(std::move(ne));
    return ret;
}

bool ConfigFile::erase(const std::string& category, const std::string& key) {
    ConfigEntry* e = find(category, key);
    if (!e) return false;
    e->deleted = true;
    e->dirty = true;
    return true;
}

bool ConfigFile::dirty() const {
    for (auto& e : entries)
        if (e->dirty) return true;
    return false;
}

std::vector<std::string> ConfigFile::categories() const {
    std::vector<std::string> out;
    for (auto& s : sections) out.push_back(s.name);
    return out;
}

bool ConfigFile::save() {
    if (!loaded) return false;
    if (!dirty()) return true;

    std::vector<std::string> out = lines;

    struct Op {
        int line;
        int endLine;
        bool isDel;
        ConfigEntry* e;
    };
    std::vector<Op> ops;
    for (auto& e : entries) {
        if (e->deleted && e->line >= 0)
            ops.push_back({e->line, e->endLine, true, e.get()});
        else if (e->dirty && e->line >= 0)
            ops.push_back({e->line, e->endLine, false, e.get()});
    }
    std::sort(ops.begin(), ops.end(), [](const Op& a, const Op& b) {
        return a.line > b.line;
    });
    for (auto& op : ops) {
        if (op.isDel) {
            out.erase(out.begin() + op.line, out.begin() + op.endLine + 1);
        } else {
            std::string& l = out[op.line];
            int len = op.endLine - op.line > 0 ? (int)l.size() : op.e->valueEnd - op.e->valueStart;
            if (op.e->valueStart + len <= (int)l.size())
                l.replace(op.e->valueStart, len, op.e->raw);
            if (op.endLine > op.line)
                out.erase(out.begin() + op.line + 1, out.begin() + op.endLine + 1);
        }
    }

    for (auto& s : sections) {
        std::vector<ConfigEntry*> added;
        size_t maxKey = 0;
        for (auto* e : s.entries) {
            if (e->key.size() > maxKey) maxKey = e->key.size();
            if (e->added && !e->deleted) added.push_back(e);
        }
        if (added.empty()) continue;
        int commentCol = (int)maxKey + 5;
        auto formatLine = [commentCol](const ConfigEntry* e) {
            std::string line = e->key + " = " + e->raw;
            if (!e->desc.empty()) {
                line += " ";
                while ((int)line.size() < commentCol) line += " ";
                line += "# " + e->desc[0];
            }
            return line;
        };
        int headerIdx = -1;
        for (int i = 0; i < (int)out.size(); i++) {
            std::string t = trimStr(out[i]);
            if (t == "[" + s.name + "]") {
                headerIdx = i;
                break;
            }
        }
        if (headerIdx == -1) {
            if (!out.empty() && !trimStr(out.back()).empty()) out.push_back("");
            out.push_back("[" + s.name + "]");
            for (auto* e : added) out.push_back(formatLine(e));
            continue;
        }
        int insertAt = headerIdx;
        for (int i = headerIdx + 1; i < (int)out.size(); i++) {
            std::string t = trimStr(out[i]);
            if (t.empty()) continue;
            if (t[0] == '[') break;
            if (t.find('=') != std::string::npos) insertAt = i;
        }
        int pos = insertAt + 1;
        for (auto* e : added) {
            out.insert(out.begin() + pos, formatLine(e));
            pos++;
        }
    }

    std::string content;
    for (size_t i = 0; i < out.size(); i++) {
        if (i) content += crlf ? "\r\n" : "\n";
        content += out[i];
    }
    if (crlf) content += "\r\n";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(content.data(), (std::streamsize)content.size());
    ofs.close();
    if (!ofs) return false;

    return load(path);
}
