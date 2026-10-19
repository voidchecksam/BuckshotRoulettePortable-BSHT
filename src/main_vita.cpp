// PS Vita entry point: the platform only (the game is app::GameApp, shared with
// the desktop). vitaGL owns the GL context, input is the controller, and
// everything measurable goes to a report file when the BSHT_REPORT switch
// file asks for one.
//
// NO %zu: newlib on arm-vita-eabi prints a literal "zu" and desynchronises every
// following vararg. Cast to unsigned long and use %lu.
#include "app/audio_wiring.h"
#include "app/debug_switch.h"
#include "app/data_screen.h"
#include "app/game_app.h"
#include "app/scene_host.h"
#include "app/loading_screen.h"
#include "app/report_files.h"
#include "app/scenario_boot.h"
#include "app/vita_defaults.h"
#include "assets/room_file.h"
#include "assets/sound_bank.h"
#include "debug/hw_probe.h"
#include "debug/report.h"
#include "platform/audio.h"
#include "platform/clock.h"
#include "platform/gl_compat.h"
#include "platform/input.h"
#include "render/post.h"
#include "render/screenshot.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/viewport_map.h"
#include "platform/vita_touch.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

namespace {

// The panel. The 3D pass renders smaller and composites up: on the Vita,
// 960x544 drops frames under the full budget and 720x408 holds 60.
constexpr int kPanelWidth   = 960;
constexpr int kPanelHeight  = 544;
constexpr int kSceneWidth   = 720;
constexpr int kSceneHeight  = 408;

const char* kReportDir = "ux0:data";   // one bsht_report_<N>.txt per boot (app/report_files.h)

// Bumped whenever the report's FORMAT changes, so an old parser fails loudly.
constexpr int kReportVersion = 3;   // v3: the hardware probe's bench and play blocks

// Stamped into line 1 of the report, so a report from another build is seen
// to be stale.
#ifndef BSHT_TITLE_ID
#define BSHT_TITLE_ID "UNKNOWN"
#endif
#ifndef BSHT_BUILD_STAMP
#define BSHT_BUILD_STAMP __DATE__ " " __TIME__
#endif
#ifndef BSHT_GIT_SHA
#define BSHT_GIT_SHA "nogit"
#endif

// Everything measurable, flushed after every milestone rather than at exit: a
// crashed run keeps what it reached, and the last line is usually the whole
// answer. Flush appends; rewriting the whole buffer cost a 223 ms frame when
// the overlay flushed per action.
struct Report {
    std::string text;
    std::string path;   // this boot's own file: a reboot never truncates a crash's report
    mutable std::size_t flushed = 0;

    void Flush() const {
        if (path.empty() || flushed >= text.size()) return;
        std::FILE* f = std::fopen(path.c_str(), flushed == 0 ? "w" : "a");
        if (!f) return;
        std::fwrite(text.data() + flushed, 1, text.size() - flushed, f);
        std::fclose(f);
        flushed = text.size();
    }

    void Line(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        text += buf;
        text += '\n';
        std::printf("%s\n", buf);   // a USB console sees it live
    }

    void Raw(const std::string& block) {
        text += block;
        std::printf("%s", block.c_str());
    }

    // A full rewrite from the beginning, for the final report.
    void Write() {
        flushed = 0;
        Flush();
    }
};

// Microseconds since the process started: the probe's frame split.
double NowMs() { return static_cast<double>(sceKernelGetProcessTimeWide()) / 1000.0; }

// The raw controller mask, before any mapping, for the report.
unsigned int g_lastPadButtons = 0;
unsigned int g_seenPadButtons = 0;

platform::FrameInput ReadController() {
    platform::FrameInput in{};
    SceCtrlData pad;
    std::memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    // A press and a release are each one event, as Godot's joypad reader
    // delivers them. Holding a button sends nothing more.
    static unsigned int previous = 0;
    const unsigned int pressed = pad.buttons & ~previous;
    const unsigned int released = previous & ~pad.buttons;
    previous = pad.buttons;
    g_lastPadButtons = pad.buttons;
    g_seenPadButtons |= pad.buttons;

    // The Vita's buttons as Godot JoyButton indices (input_enums.h:64-78), on
    // device 0, the one the original pins ui_* navigation to. CROSS is A
    // (ui_accept), CIRCLE is B (ui_cancel), START is `exit game`, which only the
    // menu reads; it does not quit.
    struct Map { unsigned int bit; int button; };
    static constexpr Map kButtons[] = {
        {SCE_CTRL_CROSS, godot::joy::A},       {SCE_CTRL_CIRCLE, godot::joy::B},
        {SCE_CTRL_SQUARE, godot::joy::X},      {SCE_CTRL_TRIANGLE, godot::joy::Y},
        {SCE_CTRL_START, godot::joy::START},   {SCE_CTRL_UP, godot::joy::DPAD_UP},
        {SCE_CTRL_DOWN, godot::joy::DPAD_DOWN}, {SCE_CTRL_LEFT, godot::joy::DPAD_LEFT},
        {SCE_CTRL_RIGHT, godot::joy::DPAD_RIGHT},
    };
    for (const Map& m : kButtons) {
        if (!((pressed | released) & m.bit)) continue;
        auto j = std::make_shared<godot::InputEventJoypadButton>();
        j->button_index = m.button;
        j->pressed = (pressed & m.bit) != 0;
        j->device = 0;
        in.events.push_back(j);
    }

    // Debug overlay intent: SELECT opens it, L/R page, the d-pad and CROSS drive it.
    if (pressed & SCE_CTRL_SELECT)   in.toggleDebug = true;
    if (pressed & SCE_CTRL_LTRIGGER) in.debugPrev = true;
    if (pressed & SCE_CTRL_RTRIGGER) in.debugNext = true;
    if (pressed & SCE_CTRL_UP)       in.navUp = true;
    if (pressed & SCE_CTRL_DOWN)     in.navDown = true;
    if (pressed & SCE_CTRL_LEFT)     in.navLeft = true;
    if (pressed & SCE_CTRL_RIGHT)    in.navRight = true;
    if (pressed & SCE_CTRL_CROSS)    in.confirm = true;
    return in;
}

bool StartPressed() {
    SceCtrlData pad;
    std::memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    return (pad.buttons & SCE_CTRL_START) != 0;
}

[[noreturn]] void Fatal(Report& report, const char* message) {
    report.Line("FATAL: %s", message);
    report.Write();
    sceKernelExitProcess(0);
    for (;;) {}
}

}  // namespace

int main(int, char**) {
    Report report;
    // Off unless ux0:data/bsht/BSHT_REPORT exists, so a player's memory card
    // does not collect a file per boot.
    const app::DebugSwitch reportSwitch = app::ReadDebugSwitch("BSHT_REPORT", BSHT_SAVE_DIR);
    report.path = app::ReportPathFor(reportSwitch.on, kReportDir);
    report.Line("bsht vita report v%d  build %s  %s  git %s", kReportVersion, BSHT_TITLE_ID,
                BSHT_BUILD_STAMP, BSHT_GIT_SHA);
    if (reportSwitch.on) report.Line("switch BSHT_REPORT on (%s)", reportSwitch.source.c_str());
    report.Flush();

    SDL_Window* window = nullptr;
    SDL_GLContext ctx = nullptr;
    if (!GlCompatInit(&window, &ctx, "bsht", kPanelWidth, kPanelHeight, /*vsync=*/true)) {
        Fatal(report, "GL init failed");
    }
    report.Line("GL_RENDERER  %s", (const char*)glGetString(GL_RENDERER));
    platform::StartTouchSampling();
    report.Line("stage: GL up");
    report.Flush();

    // SFX are a resident ADPCM bank so a gunshot starts on the exact frame;
    // music streams, since the long tracks decoded would not fit in the free
    // RAM. Opened before the room read so the menu's track covers it.
    // The game data is bsht-extract's folder; the vpk holds only the port's own files.
    const app::DataLayout layout = app::GameFolderLayout(std::string(BSHT_SAVE_DIR) + "/game", "app0:data");
    if (!app::PassDataGate(layout, "Press START to exit", [&report](const std::string& l) { report.Line("%s", l.c_str()); report.Flush(); },
                           [window] { GlCompatSwap(window); }, StartPressed)) {
        report.Write();
        GlCompatQuit(window, ctx);
        sceKernelExitProcess(0);
    }
    assets::SoundBank sfx;
    platform::Audio audio;
    if (sfx.LoadFromFile(layout.sfxBank.c_str())) {
        report.Line("audio        bank %lu sounds", (unsigned long)sfx.Count());
        if (!audio.Open(&sfx)) report.Line("audio        device FAILED to open");
    } else {
        report.Line("audio        BANK REFUSED (%s) -- the game will be silent", assets::SoundBankErrorText(sfx.LastError()));
    }
    const std::optional<std::string> scenario = app::FindDeviceScenario(BSHT_SAVE_DIR);
    const bool musicOn = app::MenuMusicAtBoot(BSHT_SAVE_DIR, false, scenario.has_value());
    const bool menuTrackRunning = musicOn && audio.PlayMusic(layout.menuMusic.c_str(), true, 0.6f, 0.6f);
    report.Line("music        %s", menuTrackRunning ? "streaming" : musicOn ? "NOT PLAYING" : "off in the saved options");

    // Ours, not VitaGL's. The 28 MB room read below is the long blocking call,
    // so the first swapped frame has to happen before it.
    app::LoadingScreen loading;
    app::BeginLoading(loading, layout.data, "Buckshot Roulette",
                      [window] { GlCompatSwap(window); },
                      [&report](const std::string& line) {
                          report.Line("%s", line.c_str());
                          report.Flush();
                      });
    loading.Show("room", 0.0f);

    const double roomStart = NowMs();
    assets::Room room;
    const std::string roomPath = app::FilesForScene(app::BootScene(scenario.has_value(), scenario.value_or("")), layout.data, layout.rooms, layout.port).room;
    const assets::RoomFileError roomErr = assets::ReadRoomFile(roomPath.c_str(), room, [&loading](float p) { loading.ShowProgress(0.75f * p); });
    if (roomErr != assets::RoomFileError::None) {
        Fatal(report, assets::RoomFileErrorText(roomErr));
    }
    report.Line("room         %d batches, %d triangles, read in %.0f ms; probe grid %dx%dx%d",
                room.Stats().batches, room.Stats().triangles, NowMs() - roomStart, room.Probes().dims[0],
                room.Probes().dims[1], room.Probes().dims[2]);
    report.Flush();
    loading.Show("textures", 0.8f);

    render::Post post;
    // Godot centres the 960x540 game on the 960x544 panel behind 2-pixel bars,
    // and touch is mapped through the same rect (platform/viewport_map.h).
    const platform::ViewportRect viewportRect =
        platform::ViewportRectFor(static_cast<float>(kPanelWidth), static_cast<float>(kPanelHeight));

    const bool havePost = post.Create(kSceneWidth, kSceneHeight, kPanelWidth, kPanelHeight);
    post.SetOutputRect(static_cast<int>(viewportRect.x), static_cast<int>(viewportRect.y),
                       static_cast<int>(viewportRect.width), static_cast<int>(viewportRect.height));
    report.Line("post         %s", havePost ? "ready" : "FAILED");

    app::AppConfig config;
    app::ApplyDataLayout(config, layout);
    config.swap = [window] { GlCompatSwap(window); };
    config.memoryReport = [] { char b[80]; std::snprintf(b, sizeof b, "free vram %lu KB, vgl ram pool %lu KB",
        (unsigned long)(vglMemFree(VGL_MEM_VRAM) / 1024), (unsigned long)(vglMemFree(VGL_MEM_RAM) / 1024)); return std::string(b); };
    app::ApplyVitaDefaults(config);   // every Vita-only choice, with its reason (app/vita_defaults.h)
    config.buildId = "build " BSHT_TITLE_ID "  " BSHT_BUILD_STAMP "  git " BSHT_GIT_SHA;
    config.sceneAspect = static_cast<float>(kSceneWidth) / static_cast<float>(kSceneHeight);
    config.windowWidth = kPanelWidth;
    config.windowHeight = kPanelHeight;
    // No cursor on the Vita: touch drives the pointer, and nothing draws one.
    config.farPlane = 2000.0f;
    if (scenario) {
        app::ApplyDeviceScenario(config, *scenario);
        report.Line("scenario: %s/scenario.json", BSHT_SAVE_DIR);
    }
    app::WireAudio(config, audio);
    config.menuTrackPath = menuTrackRunning ? layout.menuMusic : std::string();
    // Appends are cheap, and a line that is not on disk when the device is
    // switched off is a line that never happened.
    config.log = [&report](const std::string& line) {
        report.Line("%s", line.c_str());
        report.Flush();
    };
    config.title = "Buckshot Roulette";
    // Flush first: a line that is not on disk when the device switches off is a
    // line that never happened, and the report is the only evidence channel.
    config.quit = [&report] {
        report.Line("stage: QUIT from the menu");
        report.Flush();
        sceKernelExitProcess(0);
    };

    loading.Show("scripts", 0.9f);

    const double initStart = NowMs();
    app::GameApp game;
    if (!game.Init(config, std::move(room), loading.TakeFonts())) {
        Fatal(report, "the game failed to start");
    }
    loading.DestroyGl();
    // NO %zu: newlib prints a literal "zu" and desynchronises every vararg.
    report.Line("init         %.0f ms (page upload included); dyn program %s", NowMs() - initStart,
                game.Renderer().DynProgramReady() ? "compiled" : "FAILED -- flashes not drawn");
    report.Line("memory free  vram %lu KB, vgl ram pool %lu KB, phycont %lu KB", (unsigned long)(vglMemFree(VGL_MEM_VRAM) / 1024),
                (unsigned long)(vglMemFree(VGL_MEM_RAM) / 1024), (unsigned long)(vglMemFree(VGL_MEM_PHYCONT) / 1024));

    report.Line("controls: d-pad focus, CROSS accept, CIRCLE cancel, START exit (hold), SELECT debug, L/R pages");
    // The census goes in the report: it is the file that comes back.
    report.Raw(game.Census());
    report.Line("stage: entering the frame loop");
    report.Flush();

    // The hardware probe (debug/hw_probe.h): play statistics, written as it goes. Its bench
    // freezes redraw without textures or props, which shows as white flashes, so only an
    // empty ux0:data/bsht/BSHT_HW_PROBE_BENCH file turns them on.
    const bool bench = app::ReadDebugSwitch("BSHT_HW_PROBE_BENCH", BSHT_SAVE_DIR).on;
    debug::HwProbe probe(bench ? std::vector<float>{3.0f, 9.0f, 13.0f, 20.0f} : std::vector<float>{}, 30, 90);
    bool benchWritten = false, vsyncOn = true;
    float roundTime = 0.0f;
    double lastEnd = NowMs();
    platform::Clock clock;
    float totalTime = 0.0f;
    unsigned long frame = 0;

    for (;;) {
        platform::FrameInput in = ReadController();
        platform::ReadTouch(in);
        if (in.quit) break;

        const float dt = clock.Tick();
        totalTime += dt;
        ++frame;
        const bool usePost = havePost;
        const bool playing = !game.InMenu();
        if (playing) roundTime += dt;
        const debug::HwProbe::Directive d = playing && !scenario ? probe.Next(roundTime) : debug::HwProbe::Directive{};
        if (d.vsync != vsyncOn) GlCompatSetVsync(vsyncOn = d.vsync);
        game.SetProbeVariant(static_cast<int>(d.variant));

        const double t0 = NowMs();
        if (!d.freeze) game.Update(dt, in);
        const double t1 = NowMs();

        if (usePost) post.BeginScene();
        else         glViewport(static_cast<GLint>(viewportRect.x), static_cast<GLint>(viewportRect.y),
                                static_cast<GLsizei>(viewportRect.width),
                                static_cast<GLsizei>(viewportRect.height));
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        game.DrawScene();
        if (usePost) post.Composite();
        game.DrawInterface();
        if (game.PendingScenarioShot() >= 0) {
            const std::string out = app::ScenarioShotPath(BSHT_SAVE_DIR, game.ScenarioName(), game.PendingScenarioShot());
            report.Line("%s %s", render::SaveScreenshotPpm(out, kPanelWidth, kPanelHeight) ? "wrote" : "FAILED", out.c_str());
        }
        if (scenario && game.ScenarioFinished()) break;
        const double t2 = NowMs();

        // Every ten seconds, with its own worst frame.
        if (frame % 600 == 0) {
            // No %zu anywhere: newlib prints a literal "zu" and
            // desynchronises every following vararg.
            report.Line("pad          last=0x%08lX seen=0x%08lX",
                        (unsigned long)g_lastPadButtons, (unsigned long)g_seenPadButtons);
            game.RefreshTargetStats();
            report.Raw(debug::FormatFrame(game.Debug().frame));
            report.Flush();
            game.Debug().frame.worstMs = 0.0f;
        }

        GlCompatSwap(window);
        const double t3 = NowMs();

        if (playing) {
            const debug::FrameCensus& fc = game.Debug().frame;
            probe.Record({t1 - t0, t2 - t1, t3 - t2, t3 - lastEnd, fc.drawnBatches, fc.textureBinds,
                          fc.drawnTris, game.Renderer().DynActive(), roundTime});
            if (!benchWritten && probe.BenchDone()) {
                benchWritten = true;
                report.Raw(probe.FormatBench());
                report.Flush();
            }
            if (frame % 3600 == 0) {   // cumulative: the last block written is the answer
                report.Raw(probe.FormatPlay());
                report.Flush();
            }
        }
        lastEnd = t3;
    }

    report.Raw(probe.FormatBench());
    report.Raw(probe.FormatPlay());
    report.Raw(app::AudioExitSummary(audio, game));
    report.Line("ran for %.1f s, %lu frames", totalTime, frame);
    game.RefreshTargetStats();
    report.Raw(debug::FormatFrame(game.Debug().frame));
    report.Write();
    GlCompatQuit(window, ctx);
    sceKernelExitProcess(0);
    return 0;
}
