#pragma once
// 极简 JSON 解析器，仅用于本项目的 TCP 文本协议与 PLN 无关。
// 支持对象/数组/字符串/数字/布尔/null，以及简单的构造辅助。

#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0;
    std::string strVal;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    bool isNull() const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    size_t size() const {
        if (type == Type::Array) return arr.size();
        if (type == Type::Object) return obj.size();
        return 0;
    }

    const Json* get(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string str(const std::string& key, const std::string& dflt = "") const {
        const Json* j = get(key);
        if (!j || j->type != Type::String) return dflt;
        return j->strVal;
    }
    double num(const std::string& key, double dflt = 0) const {
        const Json* j = get(key);
        if (!j || j->type != Type::Number) return dflt;
        return j->numVal;
    }
    bool boolean(const std::string& key, bool dflt = false) const {
        const Json* j = get(key);
        if (!j || j->type != Type::Bool) return dflt;
        return j->boolVal;
    }
    const Json* at(size_t i) const {
        if (type != Type::Array || i >= arr.size()) return nullptr;
        return &arr[i];
    }

    std::string asString() const { return strVal; }
    double asNumber() const { return numVal; }
    bool asBool() const { return boolVal; }

    // ---- 构造辅助 ----
    static Json makeNull() { return Json(); }
    static Json makeNumber(double v) { Json j; j.type = Type::Number; j.numVal = v; return j; }
    static Json makeBool(bool v) { Json j; j.type = Type::Bool; j.boolVal = v; return j; }
    static Json makeString(std::string v) { Json j; j.type = Type::String; j.strVal = std::move(v); return j; }
    static Json makeArray() { Json j; j.type = Type::Array; return j; }
    static Json makeObject() { Json j; j.type = Type::Object; return j; }

    Json& set(std::string key, Json v) {
        if (type != Type::Object) { type = Type::Object; obj.clear(); }
        obj.emplace_back(std::move(key), std::move(v));
        return *this;
    }
    Json& push(Json v) {
        if (type != Type::Array) { type = Type::Array; arr.clear(); }
        arr.push_back(std::move(v));
        return *this;
    }

    // ---- 解析 ----
    static Json parse(const std::string& text, bool& ok) {
        const char* p = text.c_str();
        const char* end = p + text.size();
        skipWs(p, end);
        Json j = parseValue(p, end, ok);
        if (!ok) return Json();
        skipWs(p, end);
        if (p != end) ok = false;
        return ok ? j : Json();
    }

    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
            }
        }
        return out;
    }

private:
    static void skipWs(const char*& p, const char* end) {
        while (p != end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    static Json parseValue(const char*& p, const char* end, bool& ok) {
        skipWs(p, end);
        if (p == end) { ok = false; return Json(); }
        switch (*p) {
        case '{': return parseObject(p, end, ok);
        case '[': return parseArray(p, end, ok);
        case '"': { Json j = makeString(parseString(p, end, ok)); return j; }
        case 't':
            if (end - p >= 4 && strncmp(p, "true", 4) == 0) { p += 4; return makeBool(true); }
            ok = false; return Json();
        case 'f':
            if (end - p >= 5 && strncmp(p, "false", 5) == 0) { p += 5; return makeBool(false); }
            ok = false; return Json();
        case 'n':
            if (end - p >= 4 && strncmp(p, "null", 4) == 0) { p += 4; return makeNull(); }
            ok = false; return Json();
        default: return parseNumber(p, end, ok);
        }
    }

    static Json parseObject(const char*& p, const char* end, bool& ok) {
        Json j = makeObject();
        ++p; // '{'
        skipWs(p, end);
        if (p != end && *p == '}') { ++p; return j; }
        while (p != end) {
            skipWs(p, end);
            if (p == end || *p != '"') { ok = false; return Json(); }
            std::string key = parseString(p, end, ok);
            if (!ok) return Json();
            skipWs(p, end);
            if (p == end || *p != ':') { ok = false; return Json(); }
            ++p;
            Json v = parseValue(p, end, ok);
            if (!ok) return Json();
            j.obj.emplace_back(std::move(key), std::move(v));
            skipWs(p, end);
            if (p == end) { ok = false; return Json(); }
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return j; }
            ok = false;
            return Json();
        }
        ok = false;
        return Json();
    }

    static Json parseArray(const char*& p, const char* end, bool& ok) {
        Json j = makeArray();
        ++p; // '['
        skipWs(p, end);
        if (p != end && *p == ']') { ++p; return j; }
        while (p != end) {
            Json v = parseValue(p, end, ok);
            if (!ok) return Json();
            j.arr.push_back(std::move(v));
            skipWs(p, end);
            if (p == end) { ok = false; return Json(); }
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return j; }
            ok = false;
            return Json();
        }
        ok = false;
        return Json();
    }

    static Json parseNumber(const char*& p, const char* end, bool& ok) {
        const char* start = p;
        while (p != end && (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9') ||
                            *p == 'e' || *p == 'E')) ++p;
        std::string s(start, p);
        if (s.empty()) { ok = false; return Json(); }
        Json j = makeNumber(strtod(s.c_str(), nullptr));
        return j;
    }

    static std::string parseString(const char*& p, const char* end, bool& ok) {
        std::string out;
        ++p; // '"'
        while (p != end) {
            unsigned char c = (unsigned char)*p;
            if (c == '"') { ++p; return out; }
            if (c == '\\') {
                ++p;
                if (p == end) break;
                char e = *p++;
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (end - p < 4) { ok = false; return out; }
                    char hex[5] = { p[0], p[1], p[2], p[3], 0 };
                    p += 4;
                    unsigned v = (unsigned)strtoul(hex, nullptr, 16);
                    if (v < 0x80) out += (char)v;
                    else if (v < 0x800) {
                        out += (char)(0xC0 | (v >> 6));
                        out += (char)(0x80 | (v & 0x3F));
                    } else {
                        out += (char)(0xE0 | (v >> 12));
                        out += (char)(0x80 | ((v >> 6) & 0x3F));
                        out += (char)(0x80 | (v & 0x3F));
                    }
                    break;
                }
                default: out += e; break;
                }
            } else {
                out += (char)c;
                ++p;
            }
        }
        ok = false;
        return out;
    }
};
