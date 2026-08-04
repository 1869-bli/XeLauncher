#include "json.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

std::string JsonValue::lastError;

JsonValue JsonValue::makeNull() { return JsonValue(); }
JsonValue JsonValue::makeString(const std::string& s) {
    JsonValue v;
    v.type = JsonValue::String;
    v.str = s;
    return v;
}
JsonValue JsonValue::makeNumber(double n) {
    JsonValue v;
    v.type = JsonValue::Number;
    v.num = n;
    return v;
}
JsonValue JsonValue::makeBool(bool value) {
    JsonValue v;
    v.type = JsonValue::Bool;
    v.b = value;
    return v;
}
JsonValue JsonValue::makeArray() {
    JsonValue v;
    v.type = JsonValue::Array;
    return v;
}
JsonValue JsonValue::makeObject() {
    JsonValue v;
    v.type = JsonValue::Object;
    return v;
}

const JsonValue* JsonValue::get(const std::string& key) const {
    if (type != Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

JsonValue* JsonValue::get(const std::string& key) {
    if (type != Object) return nullptr;
    for (auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

static void appendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

namespace {

struct Parser {
    const char* s;
    size_t len;
    size_t i = 0;
    bool ok = true;
    std::string err;

    Parser(const std::string& text) : s(text.data()), len(text.size()) {}

    void skipWs() {
        while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    }

    void fail(const std::string& m) {
        if (ok) {
            ok = false;
            err = m;
        }
    }

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool readUnicode(unsigned int& cp) {
        if (i + 4 > len) return false;
        unsigned int v = 0;
        for (int k = 0; k < 4; k++) {
            int h = hexVal(s[i + k]);
            if (h < 0) return false;
            v = (v << 4) | (unsigned int)h;
        }
        i += 4;
        cp = v;
        return true;
    }

    JsonValue parseString() {
        JsonValue v;
        v.type = JsonValue::String;
        i++;  // opening quote
        std::string out;
        while (i < len) {
            char c = s[i];
            if (c == '"') {
                i++;
                v.str = out;
                return v;
            }
            if (c == '\\') {
                i++;
                if (i >= len) break;
                char e = s[i++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned int cp = 0;
                        if (!readUnicode(cp)) {
                            fail("bad unicode escape");
                            return v;
                        }
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (i + 1 < len && s[i] == '\\' && s[i + 1] == 'u') {
                                i += 2;
                                unsigned int lo = 0;
                                if (!readUnicode(lo)) {
                                    fail("bad low surrogate");
                                    return v;
                                }
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                } else {
                                    cp = 0xFFFD;
                                }
                            } else {
                                cp = 0xFFFD;
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail("bad escape"); return v;
                }
            } else if ((unsigned char)c < 0x20) {
                fail("control char in string");
                return v;
            } else {
                out.push_back(c);
                i++;
            }
        }
        fail("unterminated string");
        return v;
    }

    JsonValue parseNumber() {
        JsonValue v;
        v.type = JsonValue::Number;
        size_t start = i;
        if (i < len && s[i] == '-') i++;
        while (i < len && isdigit((unsigned char)s[i])) i++;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && isdigit((unsigned char)s[i])) i++;
        }
        if (i < len && (s[i] == 'e' || s[i] == 'E')) {
            i++;
            if (i < len && (s[i] == '+' || s[i] == '-')) i++;
            while (i < len && isdigit((unsigned char)s[i])) i++;
        }
        std::string tok(s + start, i - start);
        v.num = strtod(tok.c_str(), nullptr);
        return v;
    }

    JsonValue parseValue() {
        JsonValue v;
        skipWs();
        if (i >= len) {
            fail("unexpected end");
            return v;
        }
        char c = s[i];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't') {
            if (len - i >= 4 && strncmp(s + i, "true", 4) == 0) {
                i += 4;
                v.type = JsonValue::Bool;
                v.b = true;
                return v;
            }
        } else if (c == 'f') {
            if (len - i >= 5 && strncmp(s + i, "false", 5) == 0) {
                i += 5;
                v.type = JsonValue::Bool;
                v.b = false;
                return v;
            }
        } else if (c == 'n') {
            if (len - i >= 4 && strncmp(s + i, "null", 4) == 0) {
                i += 4;
                return v;
            }
        } else if (c == '-' || isdigit((unsigned char)c)) {
            return parseNumber();
        }
        fail("unexpected token");
        return v;
    }

    JsonValue parseArray() {
        JsonValue v;
        v.type = JsonValue::Array;
        i++;  // '['
        skipWs();
        if (i < len && s[i] == ']') {
            i++;
            return v;
        }
        while (i < len) {
            v.arr.push_back(parseValue());
            if (!ok) return v;
            skipWs();
            if (i < len && s[i] == ',') {
                i++;
                skipWs();
                continue;
            }
            if (i < len && s[i] == ']') {
                i++;
                return v;
            }
            fail("expected , or ]");
            return v;
        }
        fail("unterminated array");
        return v;
    }

    JsonValue parseObject() {
        JsonValue v;
        v.type = JsonValue::Object;
        i++;  // '{'
        skipWs();
        if (i < len && s[i] == '}') {
            i++;
            return v;
        }
        while (i < len) {
            skipWs();
            if (i >= len || s[i] != '"') {
                fail("expected string key");
                return v;
            }
            JsonValue key = parseString();
            if (!ok) return v;
            skipWs();
            if (i >= len || s[i] != ':') {
                fail("expected :");
                return v;
            }
            i++;
            JsonValue val = parseValue();
            if (!ok) return v;
            v.obj.emplace_back(key.str, std::move(val));
            skipWs();
            if (i < len && s[i] == ',') {
                i++;
                continue;
            }
            if (i < len && s[i] == '}') {
                i++;
                return v;
            }
            fail("expected , or }");
            return v;
        }
        fail("unterminated object");
        return v;
    }
};

void writeEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04X", c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
        }
    }
    out.push_back('"');
}

void serializeValue(std::string& out, const JsonValue& v, int indent, int depth) {
    auto nl = [&](int d) {
        if (indent < 0) return;
        out.push_back('\n');
        for (int k = 0; k < d; k++)
            for (int m = 0; m < indent; m++) out.push_back(' ');
    };
    switch (v.type) {
        case JsonValue::Null: out += "null"; break;
        case JsonValue::Bool: out += v.b ? "true" : "false"; break;
        case JsonValue::Number: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.17g", v.num);
            out += buf;
            break;
        }
        case JsonValue::String: writeEscaped(out, v.str); break;
        case JsonValue::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& e : v.arr) {
                if (!first) out.push_back(',');
                first = false;
                nl(depth + 1);
                serializeValue(out, e, indent, depth + 1);
            }
            if (!v.arr.empty()) nl(depth);
            out.push_back(']');
            break;
        }
        case JsonValue::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& kv : v.obj) {
                if (!first) out.push_back(',');
                first = false;
                nl(depth + 1);
                writeEscaped(out, kv.first);
                out.push_back(':');
                if (indent >= 0) out.push_back(' ');
                serializeValue(out, kv.second, indent, depth + 1);
            }
            if (!v.obj.empty()) nl(depth);
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

JsonValue JsonValue::parse(const std::string& text) {
    lastError.clear();
    Parser p(text);
    JsonValue v = p.parseValue();
    if (!p.ok) {
        lastError = p.err;
        return JsonValue();
    }
    p.skipWs();
    if (p.i != p.len) {
        lastError = "trailing data";
        return JsonValue();
    }
    return v;
}

std::string JsonValue::serialize(int indent) const {
    std::string out;
    serializeValue(out, *this, indent, 0);
    return out;
}
