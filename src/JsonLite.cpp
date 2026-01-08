#include "JsonLite.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>

namespace JsonLite {
namespace {

void SkipWs(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

bool ParseString(const std::string& text, size_t& pos, std::string& out, std::string* error) {
    SkipWs(text, pos);
    if (pos >= text.size() || text[pos] != '"') return false;
    ++pos;
    while (pos < text.size()) {
        char c = text[pos++];
        if (c == '"') return true;
        if (c == '\\') {
            if (pos >= text.size()) return false;
            char esc = text[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 3 >= text.size()) return false;
                    auto hex_value = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return 10 + (h - 'a');
                        if (h >= 'A' && h <= 'F') return 10 + (h - 'A');
                        return -1;
                    };
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        int v = hex_value(text[pos + i]);
                        if (v < 0) return false;
                        cp = (cp << 4) | static_cast<uint32_t>(v);
                    }
                    pos += 4;
                    if (cp <= 0x7F) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp <= 0x7FF) {
                        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else if (cp <= 0xFFFF) {
                        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default:
                    out.push_back(esc);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    if (error) *error = "Unterminated string";
    return false;
}

bool ParseNumber(const std::string& text, size_t& pos, double& out) {
    SkipWs(text, pos);
    size_t start = pos;
    if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) {
        ++pos;
    }
    bool any = false;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        any = true;
        ++pos;
    }
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            any = true;
            ++pos;
        }
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        ++pos;
        if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }
    if (!any) return false;
    const std::string slice = text.substr(start, pos - start);
    char* endPtr = nullptr;
    out = std::strtod(slice.c_str(), &endPtr);
    return endPtr && endPtr != slice.c_str();
}

bool ParseValue(const std::string& text, size_t& pos, Value& out, std::string* error);

bool ParseArray(const std::string& text, size_t& pos, Value& out, std::string* error) {
    SkipWs(text, pos);
    if (pos >= text.size() || text[pos] != '[') return false;
    ++pos;
    out.type = Type::Array;
    out.arrayValue.clear();
    SkipWs(text, pos);
    if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return true;
    }
    while (pos < text.size()) {
        Value item;
        if (!ParseValue(text, pos, item, error)) return false;
        out.arrayValue.push_back(std::move(item));
        SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            return true;
        }
        break;
    }
    if (error) *error = "Invalid array";
    return false;
}

bool ParseObject(const std::string& text, size_t& pos, Value& out, std::string* error) {
    SkipWs(text, pos);
    if (pos >= text.size() || text[pos] != '{') return false;
    ++pos;
    out.type = Type::Object;
    out.objectValue.clear();
    SkipWs(text, pos);
    if (pos < text.size() && text[pos] == '}') {
        ++pos;
        return true;
    }
    while (pos < text.size()) {
        std::string key;
        if (!ParseString(text, pos, key, error)) return false;
        SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != ':') return false;
        ++pos;
        Value value;
        if (!ParseValue(text, pos, value, error)) return false;
        out.objectValue.emplace(std::move(key), std::move(value));
        SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        break;
    }
    if (error) *error = "Invalid object";
    return false;
}

bool ParseLiteral(const std::string& text, size_t& pos, Value& out) {
    SkipWs(text, pos);
    if (text.compare(pos, 4, "true") == 0) {
        pos += 4;
        out.type = Type::Bool;
        out.boolValue = true;
        return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
        pos += 5;
        out.type = Type::Bool;
        out.boolValue = false;
        return true;
    }
    if (text.compare(pos, 4, "null") == 0) {
        pos += 4;
        out.type = Type::Null;
        return true;
    }
    return false;
}

bool ParseValue(const std::string& text, size_t& pos, Value& out, std::string* error) {
    SkipWs(text, pos);
    if (pos >= text.size()) return false;
    char c = text[pos];
    if (c == '"') {
        out.type = Type::String;
        out.stringValue.clear();
        return ParseString(text, pos, out.stringValue, error);
    }
    if (c == '{') {
        return ParseObject(text, pos, out, error);
    }
    if (c == '[') {
        return ParseArray(text, pos, out, error);
    }
    if (ParseLiteral(text, pos, out)) return true;
    double number = 0.0;
    if (ParseNumber(text, pos, number)) {
        out.type = Type::Number;
        out.numberValue = number;
        return true;
    }
    if (error) *error = "Invalid value";
    return false;
}

} // namespace

bool Parse(const std::string& text, Value& out, std::string* error) {
    size_t pos = 0;
    if (!ParseValue(text, pos, out, error)) return false;
    SkipWs(text, pos);
    if (pos != text.size()) {
        if (error) *error = "Trailing data";
        return false;
    }
    return true;
}

const Value* GetObjectValue(const Value& value, const char* key) {
    if (value.type != Type::Object || !key) return nullptr;
    auto it = value.objectValue.find(key);
    if (it == value.objectValue.end()) return nullptr;
    return &it->second;
}

std::string GetString(const Value& value, const std::string& fallback) {
    if (value.type == Type::String) return value.stringValue;
    return fallback;
}

int GetInt(const Value& value, int fallback) {
    if (value.type == Type::Number) return static_cast<int>(value.numberValue);
    if (value.type == Type::Bool) return value.boolValue ? 1 : 0;
    return fallback;
}

std::int64_t GetInt64(const Value& value, std::int64_t fallback) {
    if (value.type == Type::Number) return static_cast<std::int64_t>(value.numberValue);
    if (value.type == Type::Bool) return value.boolValue ? 1 : 0;
    return fallback;
}

double GetDouble(const Value& value, double fallback) {
    if (value.type == Type::Number) return value.numberValue;
    if (value.type == Type::Bool) return value.boolValue ? 1.0 : 0.0;
    return fallback;
}

bool GetBool(const Value& value, bool fallback) {
    if (value.type == Type::Bool) return value.boolValue;
    if (value.type == Type::Number) return value.numberValue != 0.0;
    return fallback;
}

std::string Escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", ch);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

} // namespace JsonLite
