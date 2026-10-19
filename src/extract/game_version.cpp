#include "extract/game_version.h"

#include <charconv>

namespace extract {

namespace {

struct Supported {
    const char* number;
    int hotfix;
};
constexpr Supported kSupported[] = {{"v2.2.0", 6}};

// The text after `name = ` on the line that declares it.
std::optional<std::string_view> ValueOf(std::string_view text, std::string_view name) {
    const std::string needle = "var " + std::string(name) + " = ";
    const size_t at = text.find(needle);
    if (at == std::string_view::npos) return std::nullopt;
    const size_t begin = at + needle.size();
    const size_t end = text.find('\n', begin);
    std::string_view v = text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.remove_suffix(1);
    return v;
}

}  // namespace

std::optional<GameVersion> ParseGameVersion(std::string_view gd) {
    const auto number = ValueOf(gd, "currentVersion_nr");
    const auto hotfix = ValueOf(gd, "currentVersion_hotfix");
    if (!number || !hotfix || number->size() < 2 || number->front() != '"' || number->back() != '"') {
        return std::nullopt;
    }
    GameVersion v;
    v.number = std::string(number->substr(1, number->size() - 2));
    const auto [end, ec] = std::from_chars(hotfix->data(), hotfix->data() + hotfix->size(), v.hotfix);
    if (ec != std::errc() || end != hotfix->data() + hotfix->size()) return std::nullopt;
    return v;
}

bool IsSupported(const GameVersion& v) {
    for (const Supported& s : kSupported) {
        if (v.number == s.number && v.hotfix == s.hotfix) return true;
    }
    return false;
}

std::string Describe(const GameVersion& v) { return v.number + " hotfix " + std::to_string(v.hotfix); }

std::string SupportedVersions() {
    std::string out;
    for (const Supported& s : kSupported) {
        if (!out.empty()) out += ", ";
        out += std::string(s.number) + " hotfix " + std::to_string(s.hotfix);
    }
    return out;
}

}  // namespace extract
