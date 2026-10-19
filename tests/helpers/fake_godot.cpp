// Stands in for Godot in test_godot_run: writes its arguments, working
// directory and user-data variables, as UTF-8, to fake_godot_out.txt in its
// working directory. "sleep" as the last argument makes it hang; "fail" makes
// it exit with 3.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

static std::string Utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::vector<std::string> Args() {
    int n = 0;
    wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> a;
    for (int i = 1; i < n; ++i) a.push_back(Utf8(w[i]));
    LocalFree(w);
    return a;
}

static std::string Env(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    return v ? Utf8(v) : std::string();
}
#else
#include <cstdlib>
static std::vector<std::string> gArgs;
static std::vector<std::string> Args() { return gArgs; }
static std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
    gArgs.assign(argv + 1, argv + argc);
#else
    (void)argc;
    (void)argv;
#endif
    const std::vector<std::string> args = Args();
    std::ofstream out("fake_godot_out.txt", std::ios::binary);
    for (const std::string& a : args) out << "arg " << a << "\n";
    const std::u8string cwd = std::filesystem::current_path().filename().u8string();
    out << "cwd " << std::string(cwd.begin(), cwd.end()) << "\n";
#ifdef _WIN32
    for (const wchar_t* var : {L"APPDATA", L"XDG_DATA_HOME", L"HOME"}) {
        const std::string v = Env(var);
        if (!v.empty()) out << "env " << Utf8(var) << "=" << v << "\n";
    }
#else
    for (const char* var : {"APPDATA", "XDG_DATA_HOME", "HOME"}) {
        const std::string v = Env(var);
        if (!v.empty()) out << "env " << var << "=" << v << "\n";
    }
#endif
    out.close();
    std::puts("fake godot ran");
    std::fflush(stdout);
    if (!args.empty() && args.back() == "sleep") std::this_thread::sleep_for(std::chrono::seconds(30));
    return !args.empty() && args.back() == "fail" ? 3 : 0;
}
