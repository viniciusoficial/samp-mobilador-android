#include "CefJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cefjson
{

namespace
{

constexpr int kMaxDepth = 64;

struct Parser
{
    const char* p = nullptr;
    const char* end = nullptr;
    std::string error;

    bool Fail(const char* what)
    {
        if (error.empty())
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "json: %s (offset %zu)", what, static_cast<size_t>(p - begin));
            error = buf;
        }
        return false;
    }

    const char* begin = nullptr;

    void SkipWs()
    {
        while (p < end)
        {
            const char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++p;
            else
                break;
        }
    }

    bool Literal(const char* text)
    {
        const size_t n = std::strlen(text);
        if (static_cast<size_t>(end - p) < n || std::memcmp(p, text, n) != 0)
            return Fail("bad literal");
        p += n;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t cp)
    {
        if (cp <= 0x7F)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool Hex4(uint32_t& out)
    {
        if (end - p < 4)
            return Fail("short \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = p[i];
            v <<= 4;
            if (c >= '0' && c <= '9')      v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')  v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')  v |= static_cast<uint32_t>(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        p += 4;
        out = v;
        return true;
    }

    bool ParseString(std::string& out)
    {
        // caller guarantees *p == '"'
        ++p;
        out.clear();
        while (p < end)
        {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c == '"')
            {
                ++p;
                return true;
            }
            if (c == '\\')
            {
                ++p;
                if (p >= end)
                    return Fail("unterminated escape");
                const char esc = *p++;
                switch (esc)
                {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!Hex4(cp))
                            return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 &&
                            p[0] == '\\' && p[1] == 'u')
                        {
                            const char* save = p;
                            p += 2;
                            uint32_t low = 0;
                            if (Hex4(low) && low >= 0xDC00 && low <= 0xDFFF)
                            {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            }
                            else
                            {
                                p = save;
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        return Fail("bad escape");
                }
                continue;
            }
            if (c < 0x20)
                return Fail("control character in string");
            out.push_back(static_cast<char>(c));
            ++p;
        }
        return Fail("unterminated string");
    }

    bool ParseNumber(Value& out)
    {
        const char* start = p;
        bool isFloat = false;
        if (p < end && (*p == '-' || *p == '+'))
            ++p;
        const char* digits = p;
        while (p < end && *p >= '0' && *p <= '9')
            ++p;
        if (p == digits)
            return Fail("bad number");
        if (p < end && *p == '.')
        {
            isFloat = true;
            ++p;
            const char* frac = p;
            while (p < end && *p >= '0' && *p <= '9')
                ++p;
            if (p == frac)
                return Fail("bad fraction");
        }
        if (p < end && (*p == 'e' || *p == 'E'))
        {
            isFloat = true;
            ++p;
            if (p < end && (*p == '-' || *p == '+'))
                ++p;
            const char* exp = p;
            while (p < end && *p >= '0' && *p <= '9')
                ++p;
            if (p == exp)
                return Fail("bad exponent");
        }

        const std::string text(start, static_cast<size_t>(p - start));
        if (!isFloat)
        {
            errno = 0;
            char* endPtr = nullptr;
            const long long v = std::strtoll(text.c_str(), &endPtr, 10);
            if (errno == 0 && endPtr && *endPtr == '\0')
            {
                out.type = Value::Type::Int;
                out.intValue = static_cast<int64_t>(v);
                return true;
            }
            // Out of int64 range: keep it as a double like a JS engine would.
        }
        out.type = Value::Type::Double;
        out.doubleValue = std::strtod(text.c_str(), nullptr);
        return true;
    }

    bool ParseValue(Value& out, int depth)
    {
        if (depth > kMaxDepth)
            return Fail("nesting too deep");
        SkipWs();
        if (p >= end)
            return Fail("unexpected end of input");

        const char c = *p;
        if (c == '{')
        {
            ++p;
            out.type = Value::Type::Object;
            out.object.clear();
            SkipWs();
            if (p < end && *p == '}')
            {
                ++p;
                return true;
            }
            while (true)
            {
                SkipWs();
                if (p >= end || *p != '"')
                    return Fail("expected object key");
                std::string key;
                if (!ParseString(key))
                    return false;
                SkipWs();
                if (p >= end || *p != ':')
                    return Fail("expected ':'");
                ++p;
                Value child;
                if (!ParseValue(child, depth + 1))
                    return false;
                out.object.emplace_back(std::move(key), std::move(child));
                SkipWs();
                if (p < end && *p == ',')
                {
                    ++p;
                    continue;
                }
                if (p < end && *p == '}')
                {
                    ++p;
                    return true;
                }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[')
        {
            ++p;
            out.type = Value::Type::Array;
            out.array.clear();
            SkipWs();
            if (p < end && *p == ']')
            {
                ++p;
                return true;
            }
            while (true)
            {
                Value child;
                if (!ParseValue(child, depth + 1))
                    return false;
                out.array.push_back(std::move(child));
                SkipWs();
                if (p < end && *p == ',')
                {
                    ++p;
                    continue;
                }
                if (p < end && *p == ']')
                {
                    ++p;
                    return true;
                }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"')
        {
            out.type = Value::Type::String;
            return ParseString(out.stringValue);
        }
        if (c == 't')
        {
            out.type = Value::Type::Bool;
            out.boolValue = true;
            return Literal("true");
        }
        if (c == 'f')
        {
            out.type = Value::Type::Bool;
            out.boolValue = false;
            return Literal("false");
        }
        if (c == 'n')
        {
            out.type = Value::Type::Null;
            return Literal("null");
        }
        return ParseNumber(out);
    }
};

} // namespace

bool Value::AsBool(bool fallback) const
{
    if (type == Type::Bool) return boolValue;
    if (type == Type::Int) return intValue != 0;
    if (type == Type::Double) return doubleValue != 0.0;
    return fallback;
}

int64_t Value::AsInt(int64_t fallback) const
{
    if (type == Type::Int) return intValue;
    if (type == Type::Double) return static_cast<int64_t>(doubleValue);
    if (type == Type::Bool) return boolValue ? 1 : 0;
    return fallback;
}

double Value::AsDouble(double fallback) const
{
    if (type == Type::Double) return doubleValue;
    if (type == Type::Int) return static_cast<double>(intValue);
    if (type == Type::Bool) return boolValue ? 1.0 : 0.0;
    return fallback;
}

std::string Value::AsString(const std::string& fallback) const
{
    return type == Type::String ? stringValue : fallback;
}

const Value* Value::Find(const std::string& key) const
{
    if (type != Type::Object)
        return nullptr;
    for (const auto& kv : object)
    {
        if (kv.first == key)
            return &kv.second;
    }
    return nullptr;
}

const Value* Value::Find(const char* key) const
{
    return key ? Find(std::string(key)) : nullptr;
}

const Value* Value::At(size_t index) const
{
    if (type != Type::Array || index >= array.size())
        return nullptr;
    return &array[index];
}

bool Parse(const char* data, size_t size, Value& out, std::string* error)
{
    out = Value();
    if (!data)
    {
        if (error) *error = "json: null input";
        return false;
    }

    Parser parser;
    parser.begin = data;
    parser.p = data;
    parser.end = data + size;

    if (!parser.ParseValue(out, 0))
    {
        if (error) *error = parser.error;
        return false;
    }
    parser.SkipWs();
    if (parser.p != parser.end)
    {
        parser.Fail("trailing data");
        if (error) *error = parser.error;
        out = Value();
        return false;
    }
    return true;
}

bool Parse(const std::string& text, Value& out, std::string* error)
{
    return Parse(text.data(), text.size(), out, error);
}

std::string Escape(const std::string& utf8)
{
    std::string out;
    out.reserve(utf8.size() + 2);
    out.push_back('"');
    for (const char raw : utf8)
    {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    // UTF-8 continuation bytes >= 0x80 pass through untouched.
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

} // namespace cefjson
