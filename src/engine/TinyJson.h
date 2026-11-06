// Fénix I emulation — minimal dependency-free JSON reader/writer used by the
// tunables file (resources/topology_params.json). Supports objects, arrays,
// numbers, strings, bools, null. Not a general-purpose library.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cmath>
#include <cctype>

namespace fenix {

struct JsonValue
{
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj; // ordered

    const JsonValue* get (const std::string& key) const
    {
        if (type != Object) return nullptr;
        for (auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool isNumber() const { return type == Number; }
    bool isObject() const { return type == Object; }
};

class JsonParser
{
public:
    static bool parse (const std::string& text, JsonValue& out, std::string* err = nullptr)
    {
        JsonParser p (text);
        p.ws();
        bool ok = p.value (out);
        p.ws();
        if (ok && p.pos != text.size()) { ok = false; p.error = "trailing characters"; }
        if (! ok && err) *err = p.error + " at offset " + std::to_string (p.pos);
        return ok;
    }

private:
    explicit JsonParser (const std::string& t) : s (t) {}
    const std::string& s; size_t pos = 0; std::string error;

    void ws() { while (pos < s.size() && std::isspace ((unsigned char) s[pos])) ++pos; }
    bool fail (const char* m) { error = m; return false; }

    bool value (JsonValue& v)
    {
        if (pos >= s.size()) return fail ("unexpected end");
        char c = s[pos];
        if (c == '{') return object (v);
        if (c == '[') return array (v);
        if (c == '"') { v.type = JsonValue::String; return string (v.str); }
        if (c == 't' && s.compare (pos, 4, "true") == 0)  { pos += 4; v.type = JsonValue::Bool; v.b = true;  return true; }
        if (c == 'f' && s.compare (pos, 5, "false") == 0) { pos += 5; v.type = JsonValue::Bool; v.b = false; return true; }
        if (c == 'n' && s.compare (pos, 4, "null") == 0)  { pos += 4; v.type = JsonValue::Null; return true; }
        return number (v);
    }
    bool number (JsonValue& v)
    {
        const char* start = s.c_str() + pos;
        char* end = nullptr;
        double d = std::strtod (start, &end);
        if (end == start) return fail ("bad token");
        pos += (size_t) (end - start);
        v.type = JsonValue::Number; v.num = d;
        return true;
    }
    bool string (std::string& out)
    {
        if (s[pos] != '"') return fail ("expected string");
        ++pos;
        out.clear();
        while (pos < s.size())
        {
            char c = s[pos++];
            if (c == '"') return true;
            if (c == '\\')
            {
                if (pos >= s.size()) return fail ("bad escape");
                char e = s[pos++];
                switch (e)
                {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break;
                    case 'r': out += '\r'; break; case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break; case 'u': pos += 4; out += '?'; break;
                    default: out += e; break;
                }
            }
            else out += c;
        }
        return fail ("unterminated string");
    }
    bool array (JsonValue& v)
    {
        v.type = JsonValue::Array; ++pos; ws();
        if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
        for (;;)
        {
            JsonValue e; ws();
            if (! value (e)) return false;
            v.arr.push_back (std::move (e)); ws();
            if (pos >= s.size()) return fail ("unterminated array");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == ']') { ++pos; return true; }
            return fail ("expected , or ]");
        }
    }
    bool object (JsonValue& v)
    {
        v.type = JsonValue::Object; ++pos; ws();
        if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
        for (;;)
        {
            ws(); std::string k;
            if (! string (k)) return false;
            ws(); if (pos >= s.size() || s[pos] != ':') return fail ("expected :");
            ++pos; ws();
            JsonValue e; if (! value (e)) return false;
            v.obj.emplace_back (std::move (k), std::move (e)); ws();
            if (pos >= s.size()) return fail ("unterminated object");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == '}') { ++pos; return true; }
            return fail ("expected , or }");
        }
    }
};

inline std::string jsonEscape (const std::string& in)
{
    std::string o; o.reserve (in.size() + 2);
    for (char c : in)
    {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

inline std::string jsonNumber (double d)
{
    if (std::fabs (d - std::round (d)) < 1e-9 && std::fabs (d) < 1e9)
        return std::to_string ((long long) std::llround (d));
    char buf[48]; std::snprintf (buf, sizeof (buf), "%.6g", d);
    return buf;
}

} // namespace fenix
