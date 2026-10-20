// bsht-extract: builds the port's data from your own copy of Buckshot Roulette.
#include "bake_main.h"
#include "extract/arguments.h"
#include "extract/exporters.h"
#include "extract/fingerprint.h"
#include "extract/godot_run.h"
#include "extract/pack_reader.h"
#include "extract/pipeline.h"
#include "extract/preflight.h"
#include "extract/self_path.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace {

const char* kUsage =
    "usage: bsht-extract <Buckshot Roulette.exe, or its folder> [--out DIR] [--keep-work]\n"
    "\n"
    "Reads only the file or folder you give it. It does not use the network,\n"
    "does not look at your Steam library, and writes only inside --out\n"
    "(default: bsht-data next to this program). Copy <out>/game to\n"
    "ux0:data/bsht/game/ on the Vita.\n";

fs::path Pick(const fs::path& beside, const fs::path& dev) {
    std::error_code ec;
    return fs::exists(beside, ec) ? beside : dev;
}

fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string ToUtf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

#if defined(_WIN32)
std::string Utf8(const wchar_t* w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len > 0 ? len - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// The command line as UTF-8: argv holds the ANSI code page's version.
std::vector<std::string> Utf8Args() {
    int n = 0;
    wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i) out.push_back(Utf8(w[i]));
    LocalFree(w);
    return out;
}

// True when the console is ours alone: started from Explorer, not a prompt.
bool OwnsConsole() {
    DWORD ids[2];
    return GetConsoleProcessList(ids, 2) == 1;
}

// Double-clicked with no path: ask for one instead of printing the usage.
std::string AskForGame() {
    std::printf("Drag Buckshot Roulette.exe (or its folder) into this window, then press Enter:\n> ");
    std::fflush(stdout);
    wchar_t line[2048];
    DWORD read = 0;
    if (!ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), line, 2047, &read, nullptr)) return {};
    line[read] = L'\0';
    return extract::CleanTypedPath(Utf8(line));
}

BOOL WINAPI OnConsoleEvent(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        extract::RequestStop();
        return TRUE;
    }
    return FALSE;
}

// Godot and the bakes end with this program however it ends (Ctrl+C, the
// console closed, Task Manager): it and its children share a job that is
// killed when its last handle, ours, closes.
void EndChildrenWithUs() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits) ||
        !AssignProcessToJobObject(job, GetCurrentProcess())) {
        std::printf("note: child processes may outlive this program if it is killed (job object error %lu)\n",
                    static_cast<unsigned long>(GetLastError()));
    }
}

// Started from Explorer or by dropping a file on it, the console closes with
// the program; keep it open so the result can be read.
void HoldConsoleIfOwned() {
    if (OwnsConsole()) {
        std::printf("Press Enter to close.\n");
        std::fflush(stdout);
        // Keys pressed during the run would close the window at once.
        FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
        std::getchar();
    }
}
#else
void OnSignal(int) { extract::RequestStop(); }
void HoldConsoleIfOwned() {}
#endif

// Dev option: the build fingerprint of a copy, to add it to kKnownBuilds.
int PrintFingerprint(const std::string& exe) {
    extract::PackInfo pack;
    const extract::PackError err = extract::ReadPack(exe, pack);
    if (err != extract::PackError::None) {
        std::printf("%s: %s\n", exe.c_str(), extract::PackErrorText(err));
        return 1;
    }
    std::string error;
    const std::string fingerprint = extract::PackFingerprint(exe, pack, error);
    if (!error.empty()) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", fingerprint.c_str());
    return 0;
}

int Run(const std::vector<std::string>& args) {
    extract::Options o;
    const fs::path here = extract::SelfPath().parent_path();
    o.out = here / "bsht-data";
#if defined(_WIN32)
    o.godot = Pick(here / "godot" / "godot.exe", BSHT_SOURCE_DIR "/external/godot/Godot_v4.1.4-stable_win64_console.exe");
#else
    o.godot = Pick(here / "godot" / "godot", BSHT_SOURCE_DIR "/external/godot/Godot_v4.1.4-stable_linux.x86_64");
#endif
    o.exporters = Pick(here / "exporters", BSHT_SOURCE_DIR "/tools/godot");
    const extract::Parsed parsed = extract::ParseArguments(args, o);
    switch (parsed.action) {
    case extract::Action::Help:
        std::fputs(kUsage, stdout);
        return 0;
    case extract::Action::Fingerprint:
        return PrintFingerprint(parsed.fingerprintFile);
    case extract::Action::ListExporters:
        for (const std::string& f : extract::ExporterFiles(true)) std::printf("%s\n", f.c_str());
        return 0;
    case extract::Action::Usage:
        if (!parsed.error.empty()) std::fprintf(stderr, "%s\n\n", parsed.error.c_str());
        std::fputs(kUsage, stderr);
        return 2;
    case extract::Action::Run:
        o = parsed.options;
        break;
    }
    std::error_code ec;
    if (!fs::exists(o.godot, ec)) {
        std::printf("Godot 4.1.4 was not found at %s. Unpack the release again.\n", ToUtf8(o.godot).c_str());
        return 1;
    }
    try {
        return extract::RunPipeline(o);
    } catch (const std::exception& e) {
        std::printf("bsht-extract stopped on an unexpected error: %s. The log is %s.\n", e.what(),
                    ToUtf8(fs::absolute(o.out, ec) / "extract.log").c_str());
        return 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    // The bakes run as child processes of this program (pipeline.cpp).
    if (argc >= 2 && std::strcmp(argv[1], "bake-room") == 0) return BakeRoomMain(argc - 1, argv + 1);
    if (argc >= 2 && std::strcmp(argv[1], "bake-audio") == 0) return BakeAudioMain(argc - 1, argv + 1);

#if defined(_WIN32)
    // "Ignore Ctrl+C" is inherited, and some shells start programs with it on;
    // without this the handler below never hears Ctrl+C.
    SetConsoleCtrlHandler(nullptr, FALSE);
    SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
    SetConsoleOutputCP(CP_UTF8);   // paths are printed as UTF-8
    EndChildrenWithUs();
    std::vector<std::string> args = Utf8Args();
    if (args.size() == 1 && OwnsConsole()) {
        const std::string typed = AskForGame();
        if (!typed.empty()) args.push_back(typed);
    }
#else
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    const std::vector<std::string> args(argv, argv + argc);
#endif
    const int code = Run(args);
    HoldConsoleIfOwned();
    return code;
}
