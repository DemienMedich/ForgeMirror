#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace JsonLite {

enum class Type {
    Null,
    Bool,
    Number,
    String,
    Object,
    Array
};

struct Value {
    Type type = Type::Null;
    std::string stringValue;
    double numberValue = 0.0;
    bool boolValue = false;
    std::unordered_map<std::string, Value> objectValue;
    std::vector<Value> arrayValue;
};

bool Parse(const std::string& text, Value& out, std::string* error = nullptr);
std::string Escape(const std::string& value);

const Value* GetObjectValue(const Value& value, const char* key);
std::string GetString(const Value& value, const std::string& fallback = {});
int GetInt(const Value& value, int fallback = 0);
std::int64_t GetInt64(const Value& value, std::int64_t fallback = 0);
double GetDouble(const Value& value, double fallback = 0.0);
bool GetBool(const Value& value, bool fallback = false);

} // namespace JsonLite
