#pragma once
#include <string>
#include <utility>
#include <vector>

class JsonValue {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Type type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    static JsonValue parse(const std::string& text);
    static std::string lastError;

    std::string serialize(int indent = -1) const;

    bool isNull() const { return type == Null; }
    bool isBool() const { return type == Bool; }
    bool isNumber() const { return type == Number; }
    bool isString() const { return type == String; }
    bool isArray() const { return type == Array; }
    bool isObject() const { return type == Object; }

    const JsonValue* get(const std::string& key) const;
    JsonValue* get(const std::string& key);

    std::string asString(const std::string& def = "") const {
        return type == String ? str : def;
    }
    double asNumber(double def = 0.0) const { return type == Number ? num : def; }
    bool asBool(bool def = false) const { return type == Bool ? b : def; }

    static JsonValue makeNull();
    static JsonValue makeString(const std::string& s);
    static JsonValue makeNumber(double n);
    static JsonValue makeBool(bool v);
    static JsonValue makeArray();
    static JsonValue makeObject();
};
