// A small JSON reader for the exported data.
//
// core/ must stay dependency-free so it compiles for arm-vita-eabi unchanged,
// and every input it parses is machine-generated with a fixed shape. It is
// strict about structure and reports failure rather than trying to recover:
// malformed input is a build problem, not something to paper over.
//
// Everything works on [begin, end) spans rather than null-terminated strings, so
// a nested object can be parsed by narrowing the span instead of copying it out.
//
// FindKey anchors on the opening quote of the key, so searching for "type" does
// not match "componentType". Callers still need to narrow the span to the
// tightest enclosing object, or a key from a sibling can be picked up.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace core::json {

using Span = std::pair<const char*, const char*>;

inline bool IsWs(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// Returns the position just past the colon following `"key"`, or nullptr.
inline const char* FindKey(const char* cur, const char* end, const char* key) {
    const std::size_t klen = std::strlen(key);
    for (const char* p = cur; p + klen + 2 < end; ++p) {
        if (*p != '"') continue;
        if (std::strncmp(p + 1, key, klen) != 0) continue;
        if (p[1 + klen] != '"') continue;
        const char* q = p + 2 + klen;
        while (q < end && IsWs(*q)) ++q;
        if (q < end && *q == ':') return q + 1;
    }
    return nullptr;
}

// Like FindKey, but only matches a key at the top level of the object span:
// depth 0 relative to `cur`, ignoring anything nested inside braces or
// brackets.
//
// FindKey returns the first match anywhere in the span, nested objects
// included. Godot's JSON.stringify emits dictionary keys alphabetically, so an
// animation player serialises as {animations:[...], autoplay, path, root}, and
// FindKey reading "path" from the player's span finds a nested track's path
// instead, because `animations` sorts first.
//
// Use this for any object that contains nested objects with overlapping key
// names. The plain FindKey is fine for flat records.
inline const char* FindKeyTopLevel(const char* cur, const char* end,
                                   const char* key) {
    const std::size_t klen = std::strlen(key);
    int depth = 0;
    bool inString = false;

    for (const char* p = cur; p < end; ++p) {
        if (inString) {
            if (*p == '\\') { ++p; continue; }
            if (*p == '"') inString = false;
            continue;
        }

        if (*p == '{' || *p == '[') { ++depth; continue; }
        if (*p == '}' || *p == ']') { --depth; continue; }

        if (*p != '"') continue;

        // A key at the top level of this object sits at depth 1: the object's
        // own opening brace is the only one the scan is inside.
        if (depth == 1 && p + klen + 1 < end &&
            std::strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
            const char* q = p + 2 + klen;
            while (q < end && IsWs(*q)) ++q;
            if (q < end && *q == ':') return q + 1;
        }

        // Skip the string just looked at, whether or not it matched.
        ++p;
        while (p < end && *p != '"') {
            if (*p == '\\') ++p;
            ++p;
        }
    }
    return nullptr;
}

inline bool HasKey(const char* cur, const char* end, const char* key) {
    return FindKey(cur, end, key) != nullptr;
}

// Top-level variants of the readers, for objects with nested children.
inline std::string ReadStringTopLevel(const char* cur, const char* end,
                                      const char* key) {
    const char* v = FindKeyTopLevel(cur, end, key);
    if (!v) return std::string();
    while (v < end && *v != '"') ++v;
    if (v >= end) return std::string();
    ++v;
    const char* start = v;
    while (v < end && *v != '"') ++v;
    return std::string(start, static_cast<std::size_t>(v - start));
}

// Decodes the JSON string whose opening quote is at `p`: \" \\ \/ \n \t \r \b
// \f and \uXXXX (code points below 0x80 only; anything above is written as
// '?', since the exports are ASCII). Returns the position just past the closing
// quote, or `end` when unterminated.
inline const char* DecodeString(const char* p, const char* end, std::string& out) {
    out.clear();
    if (p >= end || *p != '"') return end;
    ++p;
    while (p < end && *p != '"') {
        if (*p != '\\' || p + 1 >= end) {
            out.push_back(*p++);
            continue;
        }
        ++p;
        switch (*p) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                unsigned code = 0;
                int digits = 0;
                while (digits < 4 && p + 1 < end) {
                    const char h = p[1];
                    unsigned v = 0;
                    if (h >= '0' && h <= '9') v = static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') v = static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v = static_cast<unsigned>(h - 'A' + 10);
                    else break;
                    code = code * 16u + v;
                    ++p;
                    ++digits;
                }
                out.push_back(code < 0x80u ? static_cast<char>(code) : '?');
                break;
            }
            default: out.push_back(*p); break;   // \" \\ \/
        }
        ++p;
    }
    return p < end ? p + 1 : end;
}

// ReadStringTopLevel with the escapes decoded and an escaped quote kept inside
// the string. Use it for any value that can hold text (the raw reader is kept
// for callers that unescape themselves, such as assets/label3d.cpp).
inline std::string ReadStringTopLevelDecoded(const char* cur, const char* end, const char* key) {
    const char* v = FindKeyTopLevel(cur, end, key);
    if (!v) return std::string();
    while (v < end && IsWs(*v)) ++v;
    std::string out;
    if (v < end && *v == '"') DecodeString(v, end, out);
    return out;
}

// A bool at the top level of the object. The runtime scene export nests script
// `exports` inside each node, and a script can export a field called `visible`;
// the plain ReadBool would find that one instead of the node's own flag.
inline bool ReadBoolTopLevel(const char* cur, const char* end, const char* key,
                             bool fallback) {
    const char* v = FindKeyTopLevel(cur, end, key);
    if (!v) return fallback;
    while (v < end && IsWs(*v)) ++v;
    if (v + 4 <= end && std::strncmp(v, "true", 4) == 0) return true;
    if (v + 5 <= end && std::strncmp(v, "false", 5) == 0) return false;
    return fallback;
}

inline int ReadIntTopLevel(const char* cur, const char* end, const char* key,
                           int fallback) {
    const char* v = FindKeyTopLevel(cur, end, key);
    if (!v) return fallback;
    while (v < end && IsWs(*v)) ++v;
    if (v >= end) return fallback;
    return std::atoi(v);
}

inline double ReadNumberTopLevel(const char* cur, const char* end,
                                 const char* key, double fallback) {
    const char* v = FindKeyTopLevel(cur, end, key);
    if (!v) return fallback;
    while (v < end && IsWs(*v)) ++v;
    if (v >= end) return fallback;
    return std::atof(v);
}

inline int ReadInt(const char* cur, const char* end, const char* key, int fallback) {
    const char* v = FindKey(cur, end, key);
    if (!v) return fallback;
    while (v < end && IsWs(*v)) ++v;
    if (v >= end) return fallback;
    return std::atoi(v);
}

inline double ReadNumber(const char* cur, const char* end, const char* key,
                         double fallback) {
    const char* v = FindKey(cur, end, key);
    if (!v) return fallback;
    while (v < end && IsWs(*v)) ++v;
    if (v >= end) return fallback;
    return std::atof(v);
}

inline bool ReadBool(const char* cur, const char* end, const char* key, bool fallback) {
    const char* v = FindKey(cur, end, key);
    if (!v) return fallback;
    while (v < end && IsWs(*v)) ++v;
    if (v + 4 <= end && std::strncmp(v, "true", 4) == 0) return true;
    if (v + 5 <= end && std::strncmp(v, "false", 5) == 0) return false;
    return fallback;
}

inline std::string ReadString(const char* cur, const char* end, const char* key) {
    const char* v = FindKey(cur, end, key);
    if (!v) return std::string();
    while (v < end && *v != '"') ++v;
    if (v >= end) return std::string();
    ++v;
    const char* start = v;
    while (v < end && *v != '"') ++v;
    return std::string(start, static_cast<std::size_t>(v - start));
}

// As FindArray, but the key must be at the top level of this object.
//
// FindArray scans into nested objects. A document with {"stats":{"textures":4},
// "textures":[...]} therefore matches the COUNT inside stats, then scans
// forward for a bracket and lands on the real array by luck. That luck depends
// on key ordering, which Godot's JSON.stringify chooses alphabetically.
inline bool FindArrayTopLevel(const char* cur, const char* end, const char* key,
                              const char** outBegin, const char** outEnd) {
    const char* v = FindKeyTopLevel(cur, end, key);
    if (!v) return false;
    while (v < end && IsWs(*v)) ++v;
    if (v >= end || *v != '[') return false;
    const char* begin = v + 1;
    int depth = 1;
    const char* p = begin;
    bool inString = false;
    for (; p < end && depth > 0; ++p) {
        if (inString) {
            if (*p == '\\') { ++p; continue; }
            if (*p == '"') inString = false;
            continue;
        }
        if (*p == '"') inString = true;
        else if (*p == '[') ++depth;
        else if (*p == ']') --depth;
    }
    if (depth != 0) return false;
    *outBegin = begin;
    *outEnd = p - 1;
    return true;
}

// Span of the array value for `key`, excluding the brackets. Bracket matching is
// string-aware so a '[' inside a name cannot unbalance it.
inline bool FindArray(const char* cur, const char* end, const char* key,
                      const char** outBegin, const char** outEnd) {
    const char* v = FindKey(cur, end, key);
    if (!v) return false;
    while (v < end && *v != '[') ++v;
    if (v >= end) return false;
    const char* begin = v + 1;
    int depth = 1;
    const char* p = begin;
    bool inString = false;
    for (; p < end && depth > 0; ++p) {
        if (inString) {
            if (*p == '\\') { ++p; continue; }
            if (*p == '"') inString = false;
            continue;
        }
        if (*p == '"') inString = true;
        else if (*p == '[') ++depth;
        else if (*p == ']') --depth;
    }
    if (depth != 0) return false;
    *outBegin = begin;
    *outEnd = p - 1;
    return true;
}

// Span of the object value for `key`, INCLUDING the braces so the result can be
// passed straight back into FindKey.
inline bool FindObject(const char* cur, const char* end, const char* key,
                       const char** outBegin, const char** outEnd) {
    const char* v = FindKey(cur, end, key);
    if (!v) return false;
    while (v < end && *v != '{') ++v;
    if (v >= end) return false;
    const char* begin = v;
    int depth = 0;
    const char* p = begin;
    bool inString = false;
    for (; p < end; ++p) {
        if (inString) {
            if (*p == '\\') { ++p; continue; }
            if (*p == '"') inString = false;
            continue;
        }
        if (*p == '"') inString = true;
        else if (*p == '{') ++depth;
        else if (*p == '}') {
            --depth;
            if (depth == 0) { *outBegin = begin; *outEnd = p + 1; return true; }
        }
    }
    return false;
}

// Splits an array span into its top-level `{...}` object spans.
inline void SplitObjects(const char* begin, const char* end,
                         std::vector<Span>& out) {
    int depth = 0;
    bool inString = false;
    const char* objStart = nullptr;
    for (const char* p = begin; p < end; ++p) {
        if (inString) {
            if (*p == '\\') { ++p; continue; }
            if (*p == '"') inString = false;
            continue;
        }
        if (*p == '"') { inString = true; continue; }
        if (*p == '{') {
            if (depth == 0) objStart = p;
            ++depth;
        } else if (*p == '}') {
            --depth;
            if (depth == 0 && objStart) {
                out.emplace_back(objStart, p + 1);
                objStart = nullptr;
            }
        }
    }
}

// Reads the bare numbers out of an array span. Used for index lists and for
// glTF's flat 16-float node matrices.
inline void ReadNumberArray(const char* begin, const char* end,
                            std::vector<double>& out) {
    for (const char* p = begin; p < end; ++p) {
        if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+') {
            out.push_back(std::atof(p));
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' ||
                               *p == '.' || *p == 'e' || *p == 'E')) {
                ++p;
            }
            --p;
        }
    }
}

// Reads the string elements of an array span, preserving empty strings so the
// element INDEX stays meaningful. A mesh-name table is indexed by glTF mesh
// index, so silently dropping unnamed entries would shift every later name onto
// the wrong mesh.
inline void ReadStringArray(const char* begin, const char* end,
                            std::vector<std::string>& out) {
    const char* p = begin;
    while (p < end) {
        if (*p != '"') { ++p; continue; }
        ++p;
        const char* start = p;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) ++p;   // skip escaped char
            ++p;
        }
        out.emplace_back(start, static_cast<std::size_t>(p - start));
        if (p < end) ++p;
    }
}

inline void ReadIntArray(const char* begin, const char* end, std::vector<int>& out) {
    for (const char* p = begin; p < end; ++p) {
        if ((*p >= '0' && *p <= '9') || *p == '-') {
            out.push_back(std::atoi(p));
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '-')) ++p;
            --p;
        }
    }
}

}  // namespace core::json
