#include "extract/pipeline.h"

#include "extract/exporters.h"
#include "extract/find_game.h"
#include "extract/fingerprint.h"
#include "extract/godot_run.h"
#include "extract/ogg_rebuild.h"
#include "extract/package.h"
#include "extract/preflight.h"
#include "extract/remove_tree.h"
#include "extract/self_path.h"
#include "extract/unpack.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>

namespace fs = std::filesystem;

namespace extract {

namespace {

constexpr size_t kLogTail = 20;

std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// "res://tools/godot/export_ui.gd" -> "export_ui.gd"
std::string ScriptFile(const std::string& script) { return script.substr(script.find_last_of('/') + 1); }

// Console and extract.log, line by line.
class Log {
public:
    explicit Log(const fs::path& file) : file_(file, std::ios::app) {}
    void Line(const std::string& s) {
        std::printf("%s\n", s.c_str());
        std::fflush(stdout);
        file_ << s << "\n";
        file_.flush();
    }

private:
    std::ofstream file_;
};

// Times a stage and reports it as it ends.
class Stage {
public:
    Stage(Log& log, std::string name) : log_(log), name_(std::move(name)), start_(std::chrono::steady_clock::now()) {
        log_.Line("[" + name_ + "] started");
    }
    bool Done(bool ok, const std::string& why = "") {
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_);
        log_.Line("[" + name_ + "] " + (ok ? "ok" : "FAILED: " + why) + " (" + std::to_string(s.count()) + " s)");
        return ok;
    }
    // Ends the stage as failed; the run's exit code.
    int Failed(const std::string& why) {
        Done(false, why);
        return 1;
    }

private:
    Log& log_;
    std::string name_;
    std::chrono::steady_clock::time_point start_;
};

void ShowTail(Log& log, const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    std::deque<std::string> last;
    for (std::string line; std::getline(f, line);) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        last.push_back(line);
        if (last.size() > kLogTail) last.pop_front();
    }
    log.Line("last lines of " + Utf8(file) + ":");
    for (const std::string& l : last) log.Line("  " + l);
}

// The item scenes as SceneArgs takes them, from the pack or a dev project.
std::vector<std::string> ItemScenes(const PackInfo* pack, const fs::path& project) {
    std::vector<std::string> items;
    if (pack) {
        constexpr std::string_view kPrefix = "res://instances/item_";
        for (const PackEntry& e : pack->entries) {
            if (e.path.rfind(kPrefix, 0) != 0) continue;
            std::string rel = e.path.substr(6);   // "instances/..."
            if (rel.size() > 6 && rel.compare(rel.size() - 6, 6, ".remap") == 0) rel.resize(rel.size() - 6);
            if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".tscn") == 0) items.push_back(rel);
        }
    } else {
        std::error_code ec;
        for (fs::directory_iterator it(project / "instances", ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = Utf8(it->path().filename());
            if (n.rfind("item_", 0) == 0 && it->path().extension() == ".tscn") items.push_back("instances/" + n);
        }
    }
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
    return items;
}

bool RunExporters(Log& log, const Options& o, const fs::path& project, const fs::path& work,
                  const fs::path& extracted, const std::vector<std::string>& itemScenes) {
    Stage stage(log, "export");
    const auto runs = ExporterRuns(itemScenes);
    for (size_t i = 0; i < runs.size(); ++i) {
        std::vector<std::string> args = runs[i].args;
        args.push_back("--out=" + Utf8(extracted));
        const std::string name = ScriptFile(runs[i].script);
        ChildRun run;
        run.program = o.godot;
        run.args = GodotArgs(project, runs[i].script, args);
        run.workingDir = work;
        run.extraEnv = {UserDataVariable(ThisOs(), work / "user")};
        run.log = work / "logs" / (std::to_string(i) + "_" + name + ".log");
        log.Line("  " + name + (runs[i].args.empty() ? "" : " " + runs[i].args[0]));
        std::string error;
        if (!ClearOutputs(runs[i], extracted, error)) return stage.Done(false, error);
        const ChildResult r = RunChild(run);
        if (r.stopped) return stage.Done(false, "stopped");
        const auto missing = MissingOutputs(runs[i], extracted);
        if (!r.started || r.exitCode != 0 || !missing.empty()) {
            ShowTail(log, run.log);
            const std::string why = !r.started ? r.error
                                    : r.exitCode != 0 ? "exit code " + std::to_string(r.exitCode)
                                                      : "it did not write " + missing.front();
            return stage.Done(false, name + ": " + why);
        }
    }
    return stage.Done(true);
}

// Ogg files from Godot's packets, and the list bake_audio reads.
bool Convert(Log& log, const fs::path& extracted) {
    Stage stage(log, "convert");
    size_t count = 0;
    std::string error;
    if (!RebuildOggFiles(extracted / "audio", count, error)) return stage.Done(false, error);
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(extracted / "audio", ec), end; !ec && it != end; it.increment(ec)) {
        names.push_back(Utf8(it->path().filename()));
    }
    std::ofstream list(extracted / "audio" / "_files.txt", std::ios::binary);
    for (const std::string& n : SoundList(names)) list << "out/extracted/audio/" << n << "\n";
    list.close();
    if (!list) return stage.Done(false, "cannot write the sound list");
    log.Line("  " + std::to_string(count) + " Ogg files rebuilt");
    return stage.Done(true);
}

// The bakes, as child processes of this program, with paths relative to the
// work folder (they open files with narrow paths).
bool Bake(Log& log, const fs::path& work) {
    Stage stage(log, "bake");
    const std::vector<std::vector<std::string>> bakes = {
        {"bake-room", "--extracted", "out/extracted", "--baked", "out/baked", "--no-pages"},
        {"bake-room", "--scene", "menu", "--extracted", "out/extracted", "--baked", "out/baked", "--no-pages"},
        {"bake-room", "--scene", "heaven", "--extracted", "out/extracted", "--baked", "out/baked", "--no-pages"},
        {"bake-audio", "--dir", "out/extracted/audio", "--out", "out/baked/sfx.bank", "--list",
         "out/baked/music_list.txt", "--anims", "out/extracted/animations.json"}};
    for (size_t i = 0; i < bakes.size(); ++i) {
        ChildRun run;
        run.program = SelfPath();
        run.args = bakes[i];
        run.workingDir = work;
        run.log = work / "logs" / ("bake_" + std::to_string(i) + ".log");
        log.Line("  " + bakes[i][0] + (bakes[i][1] == "--scene" ? " " + bakes[i][2] : ""));
        const ChildResult r = RunChild(run);
        if (r.stopped) return stage.Done(false, "stopped");
        if (!r.started || r.exitCode != 0) {
            ShowTail(log, run.log);
            return stage.Done(false, bakes[i][0] + (r.started ? " exit code " + std::to_string(r.exitCode) : " " + r.error));
        }
    }
    return stage.Done(true);
}

// The checks made before anything is cleared or run; each problem is logged.
std::vector<std::string> GatherProblems(Log& log, const Options& o, const fs::path& out, const fs::path& project,
                                        const std::optional<FoundGame>& game) {
    PreflightFacts f;
    f.out = out;
    f.os = ThisOs();
    std::error_code ec;
    f.freeBytes = fs::space(out, ec).available;
    if (game) f.gameFolder = FromUtf8(game->path).parent_path();
    f.godotVersion = GodotVersion(o.godot, out);
    f.missingExporters = MissingExporters(game ? o.exporters : project / "tools" / "godot", game.has_value());
    f.foreignGameFolder = !MadeByUs(out / "game");
    f.foreignWorkFolder = !MadeByUs(out / ".work");
    log.Line("  Godot " + (f.godotVersion.empty() ? std::string("did not run") : f.godotVersion) + ", " +
             std::to_string(f.freeBytes >> 30) + " GB free on the output drive");
    return PreflightProblems(f);
}

// The game's build must be one the tool has checked: its scripts run in the
// export, so a changed copy is refused before anything is unpacked.
bool KnownBuildCheck(Log& log, const FoundGame& game) {
    std::string error;
    const std::string fingerprint = PackFingerprint(game.path, game.pack, error);
    if (!error.empty()) {
        log.Line("  " + error);
        return false;
    }
    const KnownBuild* build = FindKnownBuild(fingerprint);
    if (!build) {
        log.Line("  This copy does not match the files of " + SupportedVersions() +
                 " this tool knows. If it came from Steam, use Verify integrity of game files, then try again."
                 " (fingerprint " + fingerprint + ")");
        return false;
    }
    log.Line("  build: " + std::string(build->name));
    return true;
}

int Stopped(Log& log) {
    log.Line("stopped. Run the tool again to start over.");
    return 1;
}

// The game unpacked into a private Godot project: no Steam, no extensions,
// our exporters beside it, scripts that misbehave headless replaced.
int UnpackGame(Log& log, const Options& o, const FoundGame& game, const fs::path& project) {
    Stage unpack(log, "unpack");
    UnpackStats stats;
    std::string error;
    if (!Unpack(game.path, game.pack, project, stats, error)) return unpack.Failed(error);
    WriteOverride(project, true);
    DisableExtensions(project);
    // Without these Godot would use the player's own user folder and load GodotSteam.
    std::error_code ec;
    if (!OverrideIsOurs(project) || fs::exists(project / ".godot" / "extension_list.cfg", ec)) {
        return unpack.Failed("cannot set up the private Godot project in " + Utf8(project));
    }
    if (!CopyExporters(o.exporters, project, error)) return unpack.Failed(error);
    std::vector<std::string> replaced;
    if (!ReplaceScripts(o.exporters, project, replaced, error)) return unpack.Failed(error);
    log.Line("  " + std::to_string(stats.files) + " files, " + std::to_string(stats.bytes >> 20) + " MB");
    for (const std::string& r : replaced) log.Line("  " + r + " replaced by ours (headless it takes the OpenGL path)");
    unpack.Done(true);
    return 0;
}

// --unpack-to: the game as a reference project for Godot runs, then stop.
int UnpackForReference(Log& log, const Options& o, const FoundGame& game, const fs::path& project) {
    std::string error;
    if (!RemoveTree(project, error)) {
        log.Line("FAILED: " + error);
        return 1;
    }
    std::error_code ec;
    fs::create_directories(project, ec);
    std::ofstream(project / ".bsht-extract") << "made by bsht-extract\n";
    if (const int code = UnpackGame(log, o, game, project); code != 0) return code;
    log.Line("reference game ready in " + Utf8(project));
    return 0;
}

}  // namespace

std::vector<std::string> SoundList(std::vector<std::string> names) {
    names.erase(std::remove_if(names.begin(), names.end(),
                               [](const std::string& n) {
                                   const std::string ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
                                   return ext != ".ogg" && ext != ".wav";
                               }),
                names.end());
    std::sort(names.begin(), names.end());
    return names;
}

int RunPipeline(const Options& o) {
    std::error_code ec;
    const fs::path out = fs::absolute(o.out, ec);
    const fs::path work = out / ".work";

    // 0. Find the game before writing anything: a folder, a log or a lock
    // inside the game's folder would already be a write beside the game.
    std::optional<FoundGame> game;
    std::vector<std::string> notes;
    if (o.project.empty()) {
        FindResult found = FindGame(o.game);
        notes = std::move(found.notes);
        if (!found.game) {
            for (const std::string& note : notes) std::printf("  %s\n", note.c_str());
            std::printf("No supported copy of Buckshot Roulette was found. "
                        "Give the tool Buckshot Roulette.exe or the folder it is in.\n");
            return 1;
        }
        game = std::move(found.game);
        for (const fs::path& folder : FoldersToLeaveAlone(o.game, *game)) {
            if (IsInside(out, folder)) {
                std::printf("Choose an output folder outside %s; this tool never writes beside the game.\n",
                            Utf8(folder).c_str());
                return 1;
            }
        }
    }

    fs::create_directories(out, ec);
    if (ec) {
        std::printf("Cannot create the output folder %s: %s\n", Utf8(out).c_str(), ec.message().c_str());
        return 1;
    }
    OutputLock lock(out);
    if (!lock.Held()) {
        if (lock.Busy()) std::printf("Another bsht-extract run is using %s.\n", Utf8(out).c_str());
        else std::printf("Cannot lock the output folder: %s\n", lock.Error().c_str());
        return 1;
    }
    Log log(out / "extract.log");

    // 1. Check: the build, then everything the run needs.
    const fs::path project = !o.unpackTo.empty() ? out / "game"
                             : game              ? work / "game"
                                                 : fs::absolute(o.project, ec);
    {
        Stage check(log, "check");
        if (game) {
            for (const std::string& note : notes) log.Line("  " + note);
            log.Line("  found Buckshot Roulette " + Describe(game->version) + " in " + game->path);
            const bool known = KnownBuildCheck(log, *game);
            if (StopRequested()) return Stopped(log);
            if (!known) return check.Failed("unknown build");
        } else {
            log.Line("  using the project at " + Utf8(project));
        }
        const auto problems = GatherProblems(log, o, out, project, game);
        if (StopRequested()) return Stopped(log);
        for (const std::string& problem : problems) log.Line("  " + problem);
        if (!problems.empty()) return check.Failed("see above");
        check.Done(true);
    }
    if (StopRequested()) return Stopped(log);

    if (!o.unpackTo.empty()) return UnpackForReference(log, o, *game, project);

    // Only now is anything cleared: .work is ours (checked above).
    std::string error;
    if (!RemoveTree(work, error)) {
        log.Line("FAILED: " + error);
        return 1;
    }
    fs::create_directories(work / "logs", ec);
    std::ofstream(work / ".bsht-extract") << "made by bsht-extract\n";

    std::unique_ptr<ProjectOverride> projectOverride;
    if (game) {
        if (const int code = UnpackGame(log, o, *game, project); code != 0) return code;
    } else {
        // The project's own tools/godot are the exporters here.
        projectOverride = std::make_unique<ProjectOverride>(project);
        if (!projectOverride->Owned()) {
            log.Line("FAILED: " + Utf8(project / "override.cfg") +
                     " exists and was not written by this tool; move it away first");
            return 1;
        }
    }
    if (StopRequested()) return Stopped(log);

    // 2-4. Exports, audio, bakes.
    const fs::path extracted = work / "out" / "extracted";
    fs::create_directories(extracted, ec);
    fs::create_directories(work / "out" / "baked", ec);
    if (!RunExporters(log, o, project, work, extracted, ItemScenes(game ? &game->pack : nullptr, project))) {
        return StopRequested() ? Stopped(log) : 1;
    }
    projectOverride.reset();   // only Godot reads it; the bakes do not
    if (StopRequested()) return Stopped(log);
    if (!Convert(log, extracted)) return 1;
    if (!Bake(log, work)) return StopRequested() ? Stopped(log) : 1;
    if (StopRequested()) return Stopped(log);

    // 5. The data folder.
    {
        Stage stage(log, "package");
        const std::string version = game ? Describe(game->version) : "project " + Utf8(project);
        if (!Package(work / "out", out / "game", version, error)) return stage.Failed(error);
        stage.Done(true);
    }
    std::string leftover;
    if (!o.keepWork && !RemoveTree(work, leftover)) log.Line("  note: the work folder was left behind: " + leftover);
    log.Line("done: copy " + Utf8(out / "game") + " to ux0:data/bsht/game/ on the Vita");
    return 0;
}

}  // namespace extract
