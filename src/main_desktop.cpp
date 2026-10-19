// Desktop entry point.
//
// The platform and nothing else: the SDL window and GL context, keyboard and
// pad input, loading the room (with a build-from-source fallback the device
// does not have), the post pass, the audio device, and desktop diagnostics.
// The game itself (rendering, interface, session, debug overlay) is
// app::GameApp, shared with the Vita entry point.
//
// The window opens at 960x544, the Vita panel size, so composition and layout
// are never authored against a resolution the target does not have.
#include "app/audio_wiring.h"
#include "app/data_screen.h"
#include "app/desktop_paths.h"
#include "app/game_app.h"
#include "app/scene_host.h"
#include "app/loading_screen.h"
#include "app/orbit_bounds.h"
#include "assets/room_file.h"
#include "assets/sound_bank.h"
#include "platform/audio.h"
#include "platform/clock.h"
#include "platform/viewport_map.h"
#include "platform/window.h"
#include "render/post.h"
#include "render/screenshot.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth  = 960;
constexpr int kWindowHeight = 544;

// The 3D pass renders here and composites up. Measured on hardware: 960x544
// DROPS frames under the full budget, 720x408 HOLDS 60.
constexpr int kSceneWidth  = 720;
constexpr int kSceneHeight = 408;

const char* kAssetRoot = "assets/extracted";

std::string Join(const char* a, const char* b) {
    return std::string(a) + "/" + b;
}

// The frame grab is shared with the Vita build (render/screenshot.h).
bool SaveScreenshot(const char* path, int w, int h) { return render::SaveScreenshotPpm(path, w, h); }

// Prefer the baked file, the path the Vita uses, so desktop exercises it too.
// Fall back to building from the glTF library only when no bake exists.
bool LoadRoom(assets::Room& room, const std::string& bakedPath, float exposure,
              const std::function<void(float)>& progress) {
    const assets::RoomFileError err = assets::ReadRoomFile(bakedPath.c_str(), room, progress);
    if (err == assets::RoomFileError::None) {
        std::printf("room: loaded %s (%d batches, %d triangles)\n", bakedPath.c_str(),
                    room.Stats().batches, room.Stats().triangles);
        return true;
    }
    std::printf("room: %s unavailable (%s); building from source\n", bakedPath.c_str(),
                assets::RoomFileErrorText(err));
    if (bakedPath.find("full_world") == std::string::npos) return false;  // only main builds from source

    assets::Scene scene;
    const std::string scenePath = Join(kAssetRoot, "scene_graph.json");
    if (!scene.LoadFromFile(scenePath.c_str())) {
        std::printf("FATAL: could not load %s (run from the repo root)\n", scenePath.c_str());
        return false;
    }
    assets::MeshIndex meshIndex;
    const std::string indexPath = Join(kAssetRoot, "models/_mesh_names.json");
    if (!meshIndex.LoadFromFile(indexPath.c_str())) {
        std::printf("FATAL: could not load %s -- run tools/godot/export_scene_meshes.gd\n",
                    indexPath.c_str());
        return false;
    }
    assets::RoomBuildOptions opts;
    opts.modelDir = Join(kAssetRoot, "models");
    if (exposure >= 0.0f) opts.bake.exposure = exposure;
    if (!room.Build(scene, meshIndex, opts)) {
        std::printf("FATAL: room built empty\n");
        return false;
    }
    const assets::RoomStats& st = room.Stats();
    std::printf("room: %d/%d nodes drawn, %d batches, %d triangles\n", st.nodesDrawn,
                st.nodesConsidered, st.batches, st.triangles);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();

    const char* shotPath = nullptr;
    const char* loadingShot = nullptr;
    float shotAngle = 0.6f;
    float shotDelay = 0.0f;
    const char* scenarioPath = nullptr;   // --scenario: replay a parity scenario, grab its "shots"
    const char* dataDir = nullptr;        // --data: read bsht-extract's game folder, as the Vita does
    float exposure  = -1.0f;   // <0 keeps the BakeSettings default
    const char* uiPrefix = nullptr;
    bool dumpCensus = false, startWithOverlay = false, uncapped = false;
    bool autoplay = false, autoplaySelf = false, noCull = false, orbit = false, noPost = false, psxLook = false;
    bool skipMenu = false, keepMenu = false, quitRequested = false;
    int startBatch = 0;   // --batch N: begin at batch N (0-based); diagnostic
    const char* locale = "EN";  // --locale FR: the table the game starts in
    const char* unlockAt = "";  // --unlock ach8@3: unlock an achievement at 3 s (the toast)
    // Diagnostic: fire the menu's accept intent once, at this many seconds.
    // The menu bypasses godot::Input on purpose (hardware bug 3), so scripted
    // keys cannot reach it and a frame grab of the table past the menu needs
    // this. <0 is off.
    float menuAcceptAt = -1.0f;
    float playAt = -1.0f;
    float debugRayX = -1.0f, debugRayY = -1.0f;
    std::string playSpec;   // "PLAYER PATH|ANIMATION"
    std::string inputSpec;  // "T:key:NAME,..."
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc)            shotPath = argv[++i];
        else if (a == "--loading-shot" && i + 1 < argc) loadingShot = argv[++i];
        else if (a == "--play-at" && i + 2 < argc) {
            playAt = static_cast<float>(std::atof(argv[++i]));
            playSpec = argv[++i];
        }
        else if (a == "--angle" && i + 1 < argc)      shotAngle = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--shot-delay" && i + 1 < argc) shotDelay = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--exposure" && i + 1 < argc)   exposure = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--ui-screen" && i + 1 < argc)  uiPrefix = argv[++i];
        else if (a == "--skip-menu")                  skipMenu = true;
        else if (a == "--batch" && i + 1 < argc)      startBatch = std::atoi(argv[++i]);
        else if (a == "--locale" && i + 1 < argc)     locale = argv[++i];
        else if (a == "--unlock" && i + 1 < argc)     unlockAt = argv[++i];
        else if (a == "--scenario" && i + 1 < argc)   scenarioPath = argv[++i];
        else if (a == "--data" && i + 1 < argc)       dataDir = argv[++i];
        else if (a == "--keep-menu")                  keepMenu = true;
        else if (a == "--menu-accept" && i + 1 < argc) menuAcceptAt = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--no-post")                    noPost = true;
        else if (a == "--uncapped")                   uncapped = true;
        else if (a == "--autoplay")                   autoplay = true;
        else if (a == "--autoplay-self")              autoplay = autoplaySelf = true;
        else if (a == "--no-cull")                    noCull = true;
        else if (a == "--psx")                        psxLook = true;
        else if (a == "--orbit")                      orbit = true;
        else if (a == "--census")                     dumpCensus = true;
        else if (a == "--debug")                      startWithOverlay = true;
        else if (a == "--autoplay-input" && i + 1 < argc) inputSpec = argv[++i];
        else if (a == "--debug-ray" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%f,%f", &debugRayX, &debugRayY) != 2) debugRayX = debugRayY = -1.0f;
        }
    }

    platform::Window window;
    // Vsync OFF (--uncapped) is the only way to see true frame cost.
    if (!window.Open("Buckshot Roulette", kWindowWidth, kWindowHeight, !uncapped)) {
        std::printf("FATAL: could not open window\n");
        return 1;
    }
    std::printf("GL renderer: %s\n", (const char*)glGetString(GL_RENDERER));

    const std::string exeDir = platform::ExeFolder();
    const app::DesktopPaths paths = app::ChooseDesktopPaths(exeDir, dataDir ? dataDir : "", app::IsReleaseLayout(exeDir));
    const app::DataLayout& layout = paths.layout;
    // The data screen, as on the Vita; --loading-shot grabs it and leaves.
    if (!app::PassDataGate(layout, "Close the window to exit", [](const std::string& l) { std::printf("%s\n", l.c_str()); },
                           [&window] { window.Swap(); }, [&window, loadingShot] { return loadingShot || window.Poll().quit; },
                           [loadingShot] { if (loadingShot) SaveScreenshot(loadingShot, kWindowWidth, kWindowHeight); })) {
        return 1;
    }

    // Ours, not the driver's: the first swapped frame, before the 28 MB room
    // read blocks. The title is the same literal the window was opened with.
    app::LoadingScreen loading;
    const auto logLine = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    app::BeginLoading(loading, layout.data, "Buckshot Roulette",
                      [&window] { window.Swap(); }, logLine);
    if (loadingShot) {
        loading.SetCapture([&] {
            std::printf(SaveScreenshot(loadingShot, kWindowWidth, kWindowHeight)
                            ? "wrote %s\n" : "FAILED to write %s\n", loadingShot);
        });
    }
    loading.Show("room", 0.0f);

    app::AppConfig config;
    config.saveDir = paths.saveDir;
    // Every diagnostic wants the table, not a menu: a frame grab, an autoplayed
    // round and a scripted animation would otherwise stop at the menu. --shot
    // is one of them, since a frame comparison against Godot needs the table,
    // not a picture of the menu. --loading-shot is not here: it fires during
    // loading, before any of this. --keep-menu forces the menu back on, which
    // is how the menu is grabbed.
    config.skipMenu = !keepMenu &&
                      (skipMenu || autoplay || uiPrefix != nullptr || playAt >= 0.0f ||
                       debugRayX >= 0.0f || !inputSpec.empty() || dumpCensus ||
                       shotPath != nullptr || startBatch > 0 || scenarioPath != nullptr);
    config.swap = [&window] { window.Swap(); };
    if (scenarioPath) config.scenarioJson = app::ReadScenarioFile(scenarioPath);
    assets::Room room;
    if (!LoadRoom(room, app::FilesForScene(app::BootScene(config.skipMenu, config.scenarioJson), layout.data, layout.rooms, layout.port).room,
                  exposure, [&loading](float p) { loading.ShowProgress(0.75f * p); })) return 1;
    loading.Show("textures", 0.8f);

    app::Bounds bounds;
    for (const assets::RoomBatch& b : room.Batches()) {
        for (std::size_t i = 0; i + 2 < b.mesh.positions.size(); i += 3) {
            bounds.Add(&b.mesh.positions[i]);
        }
    }
    float centre[3] = {0.0f, 0.0f, 0.0f};
    if (bounds.Valid()) bounds.Centre(centre);
    const float radius = bounds.Valid() ? bounds.Radius() : 10.0f;

    render::Post post;
    // Godot stretches with mode `viewport` and aspect `keep`: the 960x540 game
    // is centred behind black bars. Pointer input is mapped through the same
    // rect (platform/viewport_map.h), so what is drawn and what is hit agree.
    const platform::ViewportRect viewportRect =
        platform::ViewportRectFor(static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight));

    if (!post.Create(kSceneWidth, kSceneHeight, kWindowWidth, kWindowHeight)) {
        std::printf("FATAL: post chain failed to initialise\n");
        return 1;
    }
    post.SetOutputRect(static_cast<int>(viewportRect.x), static_cast<int>(viewportRect.y),
                       static_cast<int>(viewportRect.width), static_cast<int>(viewportRect.height));

    // SFX are a resident bank; music streams from .ogg (decoded, the 32 long
    // tracks would cost 98 MB against roughly 69 MB free on the device).
    assets::SoundBank sfx;
    platform::Audio audio;
    bool menuTrackRunning = false;
    if (sfx.LoadFromFile(layout.sfxBank.c_str())) {
        audio.Open(&sfx);
        const bool musicOn = app::MenuMusicAtBoot(config.saveDir, config.skipMenu, !config.scenarioJson.empty());
        menuTrackRunning = musicOn && audio.PlayMusic(layout.menuMusic.c_str(), true, 0.6f, 0.6f);
        if (!menuTrackRunning) std::printf("audio: %s\n", musicOn ? "no music track" : "music is off in the saved options");
    } else {
        std::printf("audio: %s missing -- run ./build/src/bake_audio\n", layout.sfxBank.c_str());
    }

    loading.Show("audio", 0.85f);

    app::ApplyDataLayout(config, layout);
    config.buildId = "desktop build";
    config.sceneAspect = static_cast<float>(kSceneWidth) / static_cast<float>(kSceneHeight);
    config.farPlane = radius * 20.0f + 100.0f;
    config.cull = !noCull;
    config.psxLook = psxLook;
    config.autoplay = autoplay;
    config.autoplaySelf = autoplaySelf;
    config.uiScreenPrefix = uiPrefix;
    config.startWithOverlay = startWithOverlay;
    config.debugRayX = debugRayX;
    config.debugRayY = debugRayY;
    if (!inputSpec.empty()) {
        std::string bad;
        config.autoplayInput = app::ParseAutoplayInput(inputSpec, bad);
        if (!bad.empty()) std::printf("autoplay-input: skipped %s\n", bad.c_str());
    }
    if (const std::size_t bar = playSpec.find('|'); playAt >= 0.0f && bar != std::string::npos) {
        config.playAt = playAt;
        config.playPlayer = playSpec.substr(0, bar);
        config.playAnimation = playSpec.substr(bar + 1);
    }
    config.windowWidth = kWindowWidth;
    config.windowHeight = kWindowHeight;
    app::WireAudio(config, audio);
    config.menuTrackPath = menuTrackRunning ? layout.menuMusic : std::string();
    // CursorManager decides both; the images are the game's own.
    config.showCursor = [&window](bool visible) { window.ShowCursor(visible); };
    config.setCursor = [&window, cursors = layout.cursors](const std::string& image, godot::Vector2 hotspot) {
        if (!window.SetCursor(cursors, image, hotspot.x, hotspot.y)) {
            std::printf("cursor: %s is not in %s; the system arrow stays\n", image.c_str(), cursors.c_str());
        }
    };
    config.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    config.title = "Buckshot Roulette";
    config.startBatch = startBatch;
    config.locale = locale;
    config.debugUnlock = unlockAt;
    config.quit = [&quitRequested] { quitRequested = true; };

    loading.Show("scripts", 0.9f);

    app::GameApp game;
    if (!game.Init(config, std::move(room), loading.TakeFonts())) return 1;
    loading.DestroyGl();
    if (dumpCensus) std::printf("%s", game.Census().c_str());
    std::printf("controls: the original's bindings -- arrows / d-pad focus, ENTER / pad A accept, "
                "BACKSPACE / pad B cancel; close the window to quit\n");

    // Debug only: a wide orbit outside the room. It sees everything, which
    // makes culling look ineffective, so never judge the game from it.
    float orbitAngle = shotPath ? shotAngle : 0.0f;
    if (orbit) {
        game.SetCameraOverride(true);
        game.Camera().SetPerspective(65.5f, config.sceneAspect, 0.1f, config.farPlane);
    }

    platform::Clock clock;
    unsigned long frames = 0;
    float elapsed = 0.0f;
    float totalTime = 0.0f;

    for (;;) {
        platform::FrameInput in = window.Poll();
        if (in.quit || quitRequested) break;

        const float dt = clock.Tick();
        if (menuAcceptAt >= 0.0f && totalTime + dt >= menuAcceptAt) {
            game.ScriptMenuAccept();
            menuAcceptAt = -1.0f;   // once
        }
        elapsed += dt;
        totalTime += dt;
        ++frames;

        game.Update(dt, in);

        if (orbit) {
            if (!shotPath) orbitAngle += dt * 0.25f;
            const float dist = radius * 1.1f;
            const float eye[3] = {centre[0] + std::cos(orbitAngle) * dist,
                                  centre[1] + radius * 0.30f,
                                  centre[2] + std::sin(orbitAngle) * dist};
            game.Camera().LookAt(eye, centre);
        }

        if (noPost) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(static_cast<GLint>(viewportRect.x), static_cast<GLint>(viewportRect.y),
                       static_cast<GLsizei>(viewportRect.width), static_cast<GLsizei>(viewportRect.height));
        } else {
            post.BeginScene();
        }
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        game.DrawScene();
        if (!noPost) post.Composite();
        game.DrawInterface();

        if (game.PendingScenarioShot() >= 0) {
            const std::string out = "build/shots/port_" + game.ScenarioName() + "_" +
                                    std::to_string(game.PendingScenarioShot()) + ".ppm";
            std::printf(SaveScreenshot(out.c_str(), kWindowWidth, kWindowHeight) ? "wrote %s\n" : "FAILED %s\n",
                        out.c_str());
        }
        if (scenarioPath && game.ScenarioFinished()) break;
        // Screenshot mode: let the driver settle, grab the buffer, exit.
        if (shotPath && frames >= 3 && totalTime >= shotDelay) {
            std::printf(SaveScreenshot(shotPath, kWindowWidth, kWindowHeight)
                            ? "wrote %s\n" : "FAILED to write %s\n", shotPath);
            break;
        }

        window.Swap();

        if (elapsed >= 2.0f) {
            game.RefreshTargetStats();
            const debug::FrameCensus& f = game.Debug().frame;
            std::printf("audio: voices=%d music=%d mix last=%.0fus peak=%.0fus anim=%d missing=%d | %s\n",
                        audio.ActiveVoices(), audio.MusicPlaying() ? 1 : 0,
                        audio.LastMixMicros(), audio.PeakMixMicros(),
                        game.Tree().animation_stats().audio_plays, audio.MissingCues(), audio.Summary().c_str());
            std::printf("anim: playing=%d time=%.2f everAnimated=%d/%d played=%d missing=%d\n",
                        f.animPlaying ? 1 : 0, f.animTime, f.everAnimated, f.totalTargets,
                        f.animsPlayed, f.missingAnims);
            // Animation METHOD tracks are gameplay: EjectShell, SetupShell,
            // AttachHandCuffs and 30 more. A script without a call() dispatcher
            // drops them SILENTLY, so the counts are printed.
            {
                const godot::AnimationStats& as = game.Tree().animation_stats();
                std::printf("methods: called=%d unhandled=%d", as.methods_called,
                            as.methods_unhandled);
                for (const std::string& m : as.unhandled_methods) std::printf(" %s", m.c_str());
                std::putchar('\n');
            }
            if (game.InterfaceReady()) {
                std::printf("ui: %d quads in %d draw call(s), focus=%s\n", f.uiQuads,
                            f.uiDrawCalls, game.FocusedControlName());
            }
            std::printf("%lu frames in %.2fs (%.1f fps) | drawn %d/%d batches, %d/%d tris\n",
                        frames, elapsed, static_cast<double>(frames) / elapsed,
                        f.drawnBatches, f.totalBatches, f.drawnTris, f.totalTris);
            frames = 0;
            elapsed = 0.0f;
        }
    }

    window.Close();
    return 0;
}
