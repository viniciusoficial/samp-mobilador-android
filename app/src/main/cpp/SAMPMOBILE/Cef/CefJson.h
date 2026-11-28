#pragma once

// Minimal, dependency-free JSON reader/writer used by the omp-cef port.
//
// Two consumers, both on the wire side of the protocol:
//   1. stage 4 - WebView -> native argument arrays produced by JS
//      (window.cef.emit serializes its arguments with JSON.stringify), and
//      native -> WebView argument arrays (jsBridge.dispatch).
//   2. stage 5 - JoinResponse.manifest_json parsing.
//
// The PC client uses CefListValue (typed) instead of JSON. JSON is only the
// transport between the Android WebView and native code; the bytes that reach
// the omp-cef server are produced by CefProtocol, not by this module. Number
// typing therefore has to be recovered from the literal: "1" is an integer and
// "1.0" is a float, matching how CEF/V8 reports IsInt()/IsDouble().

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cefjson
{

struct Value
{
    enum class Type : uint8_t
    {
        Null = 0,
        Bool,
        Int,
        Double,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolValue = false;
    int64_t intValue = 0;
    double doubleValue = 0.0;
    std::string stringValue;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Int || type == Type::Double; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // Convenience accessors; numeric getters also accept the other numeric type.
    bool AsBool(bool fallback = false) const;
    int64_t AsInt(int64_t fallback = 0) const;
    double AsDouble(double fallback = 0.0) const;
    std::string AsString(const std::string& fallback = std::string()) const;

    // Object member lookup (returns nullptr when absent or not an object).
    const Value* Find(const std::string& key) const;
    const Value* Find(const char* key) const;

    size_t Size() const { return IsArray() ? array.size() : (IsObject() ? object.size() : 0); }
    const Value* At(size_t index) const;
};

// Parses a complete JSON document. Trailing non-whitespace is an error.
// Depth is capped (kMaxDepth) so hostile input cannot blow the stack.
bool Parse(const char* data, size_t size, Value& out, std::string* error = nullptr);
bool Parse(const std::string& text, Value& out, std::string* error = nullptr);

// JSON string literal *with* surrounding quotes, escaped per RFC 8259 and
// emitted as UTF-8. Invalid byte sequences are passed through unchanged
// (browsers accept them in string literals).
std::string Escape(const std::string& utf8);

} // namespace cefjson
