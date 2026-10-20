#include "extract/arguments.h"

#include "extract/preflight.h"

namespace extract {

namespace {

std::filesystem::path FromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

Parsed Usage(std::string error) {
    Parsed p;
    p.error = std::move(error);
    return p;
}

}  // namespace

Parsed ParseArguments(const std::vector<std::string>& args, const Options& defaults) {
    Parsed p;
    p.options = defaults;
    Options& o = p.options;
    bool outGiven = false;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string a = CleanArgument(args[i]);
        const bool hasValue = i + 1 < args.size();
        if (a == "--help" || a == "-h") {
            p.action = Action::Help;
            return p;
        } else if (a == "--list-exporters") {
            p.action = Action::ListExporters;
            return p;
        } else if (a == "--fingerprint" && hasValue) {
            p.action = Action::Fingerprint;
            p.fingerprintFile = CleanArgument(args[++i]);
            return p;
        } else if (a == "--out" && hasValue) {
            o.out = FromUtf8(CleanArgument(args[++i]));
            outGiven = true;
        } else if (a == "--keep-work") {
            o.keepWork = true;
        } else if (a == "--project" && hasValue) {
            o.project = FromUtf8(CleanArgument(args[++i]));
        } else if (a == "--unpack-to" && hasValue) {
            o.unpackTo = FromUtf8(CleanArgument(args[++i]));
        } else if (a == "--godot" && hasValue) {
            o.godot = FromUtf8(CleanArgument(args[++i]));
        } else if (a == "--exporters" && hasValue) {
            o.exporters = FromUtf8(CleanArgument(args[++i]));
        } else if (!a.empty() && a[0] != '-' && o.game.empty()) {
            o.game = a;
        } else {
            return Usage("unknown argument: " + a);
        }
    }
    if (!o.unpackTo.empty()) {
        if (!o.project.empty() || o.game.empty()) return Usage("--unpack-to needs the game, not --project");
        if (outGiven) return Usage("--unpack-to is the output folder; leave out --out");
        o.out = o.unpackTo;
    }
    if (o.game.empty() && o.project.empty()) return Usage("");
    p.action = Action::Run;
    return p;
}

std::string CleanTypedPath(std::string line) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!line.empty() && blank(line.back())) line.pop_back();
    std::size_t start = 0;
    while (start < line.size() && blank(line[start])) ++start;
    line.erase(0, start);
    if (line.size() >= 2 && (line.front() == '"' || line.front() == '\'') && line.back() == line.front()) {
        line = line.substr(1, line.size() - 2);
    }
    return line;
}

}  // namespace extract
