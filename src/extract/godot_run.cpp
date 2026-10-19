#include "extract/godot_run.h"

#include <reproc/reproc.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <cctype>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
extern char** environ;
#endif

namespace extract {

namespace {

std::atomic<bool> gStop{false};

constexpr int kPollMs = 250;
constexpr int kTerminateGraceMs = 3000;
constexpr std::chrono::seconds kVersionTimeout{60};

std::string Utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// "NAME=value" -> "NAME". Windows keeps per-drive entries like "=C:=C:\x",
// whose name starts with '='.
std::string_view NameOf(std::string_view entry) {
    const size_t eq = entry.find('=', 1);
    return entry.substr(0, eq == std::string_view::npos ? entry.size() : eq);
}

bool SameName(std::string_view a, std::string_view b, bool ignoreCase) {
    if (!ignoreCase) return a == b;
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::vector<std::string> ParentEnvironment() {
    std::vector<std::string> out;
#if defined(_WIN32)
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) return out;
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, p, -1, s.data(), n, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    FreeEnvironmentStringsW(block);
#else
    for (char** e = environ; e && *e; ++e) out.emplace_back(*e);
#endif
    return out;
}

}  // namespace

std::vector<std::string> MergeEnvironment(const std::vector<std::string>& parent,
                                          const std::vector<std::string>& extra, bool ignoreCase) {
    std::vector<std::string> out;
    for (const std::string& p : parent) {
        const bool replaced = std::any_of(extra.begin(), extra.end(), [&](const std::string& e) {
            return SameName(NameOf(p), NameOf(e), ignoreCase);
        });
        if (!replaced) out.push_back(p);
    }
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

HostOs ThisOs() {
#if defined(_WIN32)
    return HostOs::Windows;
#elif defined(__APPLE__)
    return HostOs::MacOs;
#else
    return HostOs::Linux;
#endif
}

// Godot 4.1 takes user:// from APPDATA on Windows, XDG_DATA_HOME on Linux and
// ~/Library/Application Support on macOS (so HOME there).
std::string UserDataVariable(HostOs os, const std::filesystem::path& userData) {
    const char* name = os == HostOs::Windows ? "APPDATA" : os == HostOs::Linux ? "XDG_DATA_HOME" : "HOME";
    return std::string(name) + "=" + Utf8(userData);
}

std::vector<std::string> GodotArgs(const std::filesystem::path& project, const std::string& script,
                                   const std::vector<std::string>& userArgs) {
    std::vector<std::string> a = {"--headless", "--fixed-fps", "60", "--path", Utf8(project), "--script", script};
    if (!userArgs.empty()) {
        a.push_back("--");
        a.insert(a.end(), userArgs.begin(), userArgs.end());
    }
    return a;
}

ChildResult RunChild(const ChildRun& run) {
    ChildResult result;
    const std::string program = Utf8(run.program);
    const std::string cwd = Utf8(run.workingDir);
    const std::string log = Utf8(run.log);
    std::vector<const char*> argv = {program.c_str()};
    for (const std::string& a : run.args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    // The child gets a full environment of our making: with REPROC_ENV_EXTEND
    // the parent's APPDATA would come first and win over ours.
    const std::vector<std::string> merged =
        MergeEnvironment(ParentEnvironment(), run.extraEnv, ThisOs() == HostOs::Windows);
    std::vector<const char*> env;
    for (const std::string& e : merged) env.push_back(e.c_str());
    env.push_back(nullptr);

    reproc_options options = {};
    options.working_directory = cwd.c_str();
    options.env.behavior = REPROC_ENV_EMPTY;
    options.env.extra = env.data();
    options.redirect.in.type = REPROC_REDIRECT_DISCARD;   // nothing ever writes to it
    options.redirect.out.type = REPROC_REDIRECT_PATH;
    options.redirect.out.path = log.c_str();
    options.redirect.err.type = REPROC_REDIRECT_STDOUT;

    reproc_t* child = reproc_new();
    const int started = reproc_start(child, argv.data(), options);
    if (started < 0) {
        result.error = reproc_strerror(started);
        reproc_destroy(child);
        return result;
    }
    result.started = true;
    // Wait in short steps so a timeout or Ctrl+C can end the child.
    const auto deadline = std::chrono::steady_clock::now() + run.timeout;
    int status = REPROC_ETIMEDOUT;
    while (status == REPROC_ETIMEDOUT) {
        status = reproc_wait(child, kPollMs);
        if (status != REPROC_ETIMEDOUT) break;
        result.stopped = StopRequested();
        result.timedOut = std::chrono::steady_clock::now() >= deadline;
        if (result.stopped || result.timedOut) {
            reproc_terminate(child);
            if (reproc_wait(child, kTerminateGraceMs) == REPROC_ETIMEDOUT) reproc_kill(child);
            reproc_wait(child, REPROC_INFINITE);
            result.error = result.stopped ? "stopped" : "no answer after " +
                std::to_string(std::chrono::duration_cast<std::chrono::minutes>(run.timeout).count()) + " min";
            break;
        }
    }
    result.exitCode = status;
    if (status < 0 && result.error.empty()) result.error = reproc_strerror(status);
    reproc_destroy(child);
    return result;
}

std::string GodotVersion(const std::filesystem::path& godot, const std::filesystem::path& scratchDir) {
    ChildRun run;
    run.program = godot;
    run.args = {"--version"};
    run.workingDir = scratchDir;
    run.log = scratchDir / ".godot-version.log";
    run.timeout = kVersionTimeout;
    const ChildResult r = RunChild(run);
    std::string first;
    {
        std::ifstream f(run.log, std::ios::binary);
        std::getline(f, first);
    }
    std::error_code ec;
    std::filesystem::remove(run.log, ec);
    while (!first.empty() && (first.back() == '\r' || first.back() == ' ')) first.pop_back();
    return r.started && r.exitCode == 0 ? first : std::string();
}

void RequestStop() { gStop = true; }
bool StopRequested() { return gStop; }
void ClearStop() { gStop = false; }

}  // namespace extract
