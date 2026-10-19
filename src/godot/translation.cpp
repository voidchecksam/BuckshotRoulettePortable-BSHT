#include "godot/translation.h"

#include <algorithm>

#include "core/json_lite.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace godot {
namespace {

using Table = std::vector<std::pair<std::string, std::string>>;

Table& Messages() {
    static Table t;
    return t;
}

std::string& Locale() {
    static std::string l;
    return l;
}

std::string& Root() {
    static std::string r;
    return r;
}

unsigned& Generation() {
    static unsigned g = 0;
    return g;
}

Table& Suffixes() {
    static Table s;
    return s;
}

bool ReadWholeFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[8192];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// The JSON string starting at the opening quote `p`, escapes decoded. Returns
// the position just past the closing quote, or `end` if unterminated.
const char* ReadJsonString(const char* p, const char* end, std::string& out) {
    out.clear();
    ++p;
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) {
            ++p;
            switch (*p) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': break;
                default:  out.push_back(*p); break;   // escaped quote, backslash, slash
            }
        } else {
            out.push_back(*p);
        }
        ++p;
    }
    return p < end ? p + 1 : end;
}

// Every "key": "value" pair directly inside the flat messages object.
void ReadStringPairs(const char* p, const char* end, Table& out) {
    std::string key;
    std::string value;
    while (p < end) {
        if (*p != '"') {
            ++p;
            continue;
        }
        p = ReadJsonString(p, end, key);
        while (p < end && *p != ':' && *p != '"' && *p != '}') ++p;
        if (p >= end || *p != ':') continue;
        ++p;
        while (p < end && core::json::IsWs(*p)) ++p;
        if (p >= end || *p != '"') continue;
        p = ReadJsonString(p, end, value);
        out.emplace_back(key, value);
    }
}

}  // namespace

namespace {

// The table and locale in `path`, or false when it is missing or empty.
bool ReadTable(const char* path, Table& messages, std::string& locale) {
    std::string text;
    if (!ReadWholeFile(path, text)) return false;
    const char* b = nullptr;
    const char* e = nullptr;
    if (!core::json::FindObject(text.data(), text.data() + text.size(), "messages", &b, &e)) {
        return false;
    }
    // Skip the object's own opening brace so it is not mistaken for a pair.
    ReadStringPairs(b + 1, e, messages);
    locale = core::json::ReadStringTopLevelDecoded(text.data(), text.data() + text.size(), "locale");
    return !messages.empty();
}

}  // namespace

bool load_translations(const char* path) {
    ++Generation();
    Messages().clear();
    Table messages;
    std::string locale;
    if (!ReadTable(path, messages, locale)) return false;
    Messages() = std::move(messages);
    Locale() = locale;
    return true;
}

void set_translation_root(const std::string& directory) {
    Root() = directory;
}

std::string locale_file_code(const std::string& locale) {
    std::string code;
    for (char c : locale) {
        if (c == ' ') code.push_back('_');
        else if (c >= 'A' && c <= 'Z') code.push_back(static_cast<char>(c - 'A' + 'a'));
        else code.push_back(c);
    }
    return code;
}

bool set_locale(const std::string& locale) {
    const std::string path = Root() + "/dialogue_" + locale_file_code(locale) + ".json";
    Table messages;
    std::string fileLocale;
    if (!ReadTable(path.c_str(), messages, fileLocale)) {
        // TranslationServer.set_locale takes any code: with no table for it
        // get_locale() reports it and tr() returns every key as itself
        // (measured, Godot 4.1.4). The previous language does not stay.
        Messages() = Table();
        Locale() = locale;
        ++Generation();
        return false;
    }
    Messages() = std::move(messages);
    Locale() = fileLocale.empty() ? locale : fileLocale;
    ++Generation();
    return true;
}

int utf8_length(std::string_view text) {
    int n = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::size_t utf8_prefix_bytes(std::string_view text, int chars) {
    if (chars <= 0) return 0;
    int seen = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            if (seen == chars) return i;
            ++seen;
        }
    }
    return text.size();
}

std::string get_locale() {
    return Locale();
}

std::string tr(const std::string& key) {
    std::string suffix;
    for (const auto& s : Suffixes()) {
        if (s.first == key) suffix = s.second;
    }
    for (const auto& m : Messages()) {
        if (m.first == key) return m.second + suffix;
    }
    return key + suffix;
}

void set_message_suffix(const std::string& key, const std::string& suffix) {
    Table& suffixes = Suffixes();
    suffixes.erase(std::remove_if(suffixes.begin(), suffixes.end(),
                                  [&key](const auto& s) { return s.first == key; }),
                   suffixes.end());
    if (!suffix.empty()) suffixes.emplace_back(key, suffix);
    ++Generation();
}

unsigned translation_generation() {
    return Generation();
}

}  // namespace godot

namespace godot {

// ustring.cpp String::sprintf, for %s and %%: text is copied, each %s takes the
// next argument. A %s with no argument left keeps the text so far (Godot then
// reports "not enough arguments for format string" and the % yields that error
// string; no translated script reaches it).
std::string format(std::string_view fmt, std::initializer_list<std::string_view> args) {
    std::string out;
    out.reserve(fmt.size());
    auto next = args.begin();
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) {
            out += fmt[i];
            continue;
        }
        const char c = fmt[++i];
        if (c == '%') {
            out += '%';
        } else if (c == 's' && next != args.end()) {
            out += *next++;
        } else {
            out += '%';
            out += c;
        }
    }
    return out;
}

}  // namespace godot
