#include "app/game_app.h"

#include "app/debug_switch.h"
#include "app/device_features.h"
#include "app/music_cutoff.h"
#include "app/ui_screens.h"
#include "app/port_branding.h"
#include "assets/raw_image.h"

#include "app/loading_screen.h"
#include "core/heap_ledger.h"
#include "core/json_lite.h"
#include "game/local_board.h"
#include "assets/scene_fog.h"
#include "assets/room_file.h"
#include "assets/light_model.h"
#include <cstdlib>
#include <fstream>

#include "godot/input.h"
#include "godot/print.h"
#include "godot/input_map.h"

#include "debug/report.h"
#include "godot/animation_player.h"
#include "godot/camera_projection.h"
#include "godot/control.h"
#include "godot/packed_scene.h"
#include "scripts/AchievementManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/MenuManager.h"
#include "scripts/SaveFileManager.h"
#include "scripts/DonUnlockManager.h"
#include "scripts/EndingManager.h"
#include "scripts/UserExit.h"
#include "scripts/LeaderboardManager.h"
#include "scripts/OptionsManager.h"
#include "godot/audio_server.h"
#include "godot/material.h"
#include "godot/math_funcs.h"
#include "scripts/item_scenes.h"
#include "godot/runtime_scene_loader.h"
#include "godot/translation.h"
#include "platform/gl_compat.h"
#include "platform/viewport_map.h"
#include "scripts/CameraManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/FilterController.h"
#include "scripts/InteractionManager.h"
#include "scripts/MusicManager.h"
#include "scripts/ResetManager.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace app {

namespace {

// A key press or release on device 0, as project.godot pins the bindings.
godot::InputEventRef KeyEvent(std::int64_t code, bool pressed) {
    auto k = std::make_shared<godot::InputEventKey>();
    k->keycode = code;
    k->pressed = pressed;
    k->device = 0;
    return k;
}

}  // namespace

namespace {

std::string Join(const std::string& root, const char* file) {
    return root + "/" + file;
}

// The table interface: every bracket (the table's shotgun and item grids, the
// shooting choice, ...), the DEALER / SELF text, and the item description
// PickupIndicator writes. The scene tree decides which of them is on screen
// (ui_screen.h), as Godot does.
constexpr const char* kDialogueScreen = "Camera/dialogue UI/main dialogue";

// "heap: <stage>, new live X KB in N blocks (+H KB headers), peak Y KB[; newlib ...]"
// (core/heap_ledger.h): C++ new bytes, plus newlib's mallinfo on the Vita. The peak is the
// highest live since the last reset: a scene change resets it first, so its
// lines carry the change's own peak. Nothing is logged without a ledger.
void LogHeap(const std::function<void(const std::string&)>& log, const char* stage, bool resetPeak = false) {
    core::HeapLedger* ledger = core::RegisteredHeapLedger();
    if (!ledger || !log) return;
    if (resetPeak) ledger->ResetPeak();
    char line[192];
    std::snprintf(line, sizeof line, "heap: %s, new live %lu KB in %lu blocks (+%lu KB headers), peak %lu KB", stage,
                  static_cast<unsigned long>(ledger->Live() / 1024), static_cast<unsigned long>(ledger->Blocks()),
                  static_cast<unsigned long>(ledger->HeaderBytes() / 1024),
                  static_cast<unsigned long>(ledger->Peak() / 1024));
    const core::MallocReport newlib = core::RegisteredMallocReport();
    log(newlib ? std::string(line) + "; " + newlib() : std::string(line));
}

std::string Printf(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

}  // namespace

GameApp::~GameApp() { godot::set_print_sink(nullptr); }

void GameApp::Log(const std::string& line) const {
    if (config_.log) config_.log(line);
}

bool GameApp::Init(const AppConfig& config, assets::Room room, assets::FontAtlas fonts) {
    // The entry point read the atlas before the room so the loading screen
    // could draw with it. Take ownership here: LoadInterface uploads it.
    fonts_ = std::move(fonts);
    config_ = config;
    room_ = std::move(room);
    const std::string& root = config_.dataRoot;

    // The boot scene: the menu, or main when a diagnostic wants the table.
    // The entry point has already read its room.
    scene_ = FilesForScene(BootScene(config_.skipMenu, config_.scenarioJson), root, config_.roomRoot, config_.portRoot);
    Log("scene: boot into " + scene_.name);
    // The scripts' own print() lines.
    godot::set_print_sink([this](const std::string& line) { Log("game: " + line); });
    scripts::GlobalVariables()._ready();   // the autoload, once

    saves_ = std::make_unique<game::SaveStore>(config_.saveDir);
    achievements_.SetLog([this](const std::string& line) { Log(line); });
    if (!achievements_.LoadText(config_.achievementsPath.c_str())) {
        Log("ach: " + config_.achievementsPath + " missing -- unlocks are refused");
    }
    // A diagnostic or a replayed scenario never writes the player's
    // achievements: the same rule as SaveFileManager's store.
    achievements_.Attach(UsesSaveStore(config_.skipMenu, !config_.scenarioJson.empty()) ? saves_.get() : nullptr);

    // --- the room on the GPU ----------------------------------------------
    // Prop batching and GPU skinning: device defaults + switches, decided once
    // here and logged (app/device_features.h).
    const FeatureVerdict batching = DecideDeviceFeature(DeviceFeature::PropBatching);
    const FeatureVerdict gpuSkin = DecideDeviceFeature(DeviceFeature::GpuSkinning);
    renderer_.SetPropBatching(batching.on, batching.why);
    renderer_.SetGpuSkinning(gpuSkin.on, gpuSkin.why);
    if (const DebugSwitch s = ReadDebugSwitch("BSHT_SCENE_SOAK", config_.saveDir); s.on && config_.scenarioJson.empty()) {
        soak_.emplace(/*cycles=*/5, /*secondsPerScene=*/4.0f);
        Log("soak: menu <-> main 5 times, 4 s each, then quit (switch BSHT_SCENE_SOAK -- " + s.source + ")");
    }
    if (const DebugSwitch s = ReadDebugSwitch("BSHT_MENU_ACCEPT", config_.saveDir); s.on && config_.scenarioJson.empty()) {
        menuAcceptIn_ = 5.0f;
        Log("menu: START is pressed 5 s into the menu (switch BSHT_MENU_ACCEPT -- " + s.source + ")");
    }
    if (!renderer_.BuildShaders()) {
        Log("FATAL: PSX shader failed to build");
        return false;
    }
    tree_->set_profiler(&profiler_);
    renderer_.SetProfiler(&profiler_);
    {
        const godot::Color& live = scripts::GlobalVariables::default_color_live;
        const godot::Color& blank = scripts::GlobalVariables::default_color_blank;
        renderer_.SetHeldColours({{live.r, live.g, live.b}, {blank.r, blank.g, blank.b}});
    }
    // The executable's operator new counter (platform/alloc_count.cpp), if linked.
    profiler_.SetAllocCounter(core::RegisteredAllocCounter());
    Log(core::RegisteredAllocCounter() ? "prof: counting allocations per frame" : "prof: no allocation counter linked");
    if (!renderer_.Upload(room_)) {
        Log("FATAL: nothing uploaded");
        return false;
    }
    renderer_.SetCullingEnabled(config_.cull);
    if (config_.psxLook) {
        renderer_.SetSnap(320.0f, 240.0f);
        renderer_.SetAffine(true);
    }
    Log(Printf("gpu: %d batches, %d triangles uploaded (culling %s); props %lu vertices (%lu with baked a_col)",
               renderer_.BatchCount(), renderer_.TriangleCount(), config_.cull ? "on" : "OFF",
               static_cast<unsigned long>(renderer_.PropVertices()),
               static_cast<unsigned long>(renderer_.PropColouredVertices())));
    Log(renderer_.PropBatching() ? Printf("props: batching on, %d chunks", renderer_.PropChunks())
                                 : "props: batching OFF -- " + renderer_.PropBatchingOff());
    Log(renderer_.GpuSkinning() ? std::string("skin: GPU skinning on")
                                : "skin: GPU skinning OFF -- " + renderer_.GpuSkinningOff());
    Log(Printf("gpu: %d/%d room textures uploaded%s", renderer_.TexturesUploaded(), renderer_.TexturesTotal(),
               renderer_.TexturesUploaded() == renderer_.TexturesTotal() ? ""
                                                                      : " -- FAILED: those surfaces draw WHITE"));

    // --- the scene as Godot left it after _ready ----------------------------
    // Transforms in radians, visibility, render layers, transparency, material
    // alpha, the camera's cull mask and every AnimationPlayer's state, exported
    // from the live engine. The renderer reads it; animation runs in it.
    // A --scenario replays a parity scenario, whose Godot reference seed(0)s
    // before instantiating main.tscn (dump_state.gd:112): pin _ready's draws
    // the same way. A normal boot keeps the tree's random seed.
    if (!config_.scenarioJson.empty()) godot::seed(0);
    const godot::RuntimeSceneError sceneErr = godot::LoadRuntimeScene(scene_.runtime.c_str(), *tree_);
    if (sceneErr != godot::RuntimeSceneError::None) {
        Log(Printf("FATAL: %s: %s", scene_.runtime.c_str(), godot::RuntimeSceneErrorText(sceneErr)));
        return false;
    }
    if (scene_.name == "menu") Log(PortBranding::Describe(PortBranding::Apply(*tree_)));
    const int unbound = renderer_.BindTree(*tree_);
    dynLights_.Bind(*tree_);
    envGrade_.Bind(*tree_);
    movingLight_.Bind(*tree_);
    BindParticles();
    Log(Printf("scene: %lu nodes, %d props without a node",
               static_cast<unsigned long>(tree_->get_node_count()), unbound));
    BindAnimation();
    BindScripts();
    BindInput();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // Much of the room is single-sided walls authored to be seen from inside;
    // culling them punches holes in the room.
    glDisable(GL_CULL_FACE);

    // --- the interface ------------------------------------------------------
    // Not fatal: a round with no interface still plays, and the log says why.
    uiReady_ = LoadInterface();

    // --- what the dealer says, where the camera is, the 3D labels ----------
    // All game content, read at runtime, never compiled in. The dialogue box
    // and the camera are the translated DialogueManager and CameraManager.
    if (!dialogueScript_) Log("dialogue: no DialogueManager script -- the dealer will not speak");
    SnapCameraHome();

    if (!scene_.labels.empty()) labelData_.LoadFromFile(scene_.labels.c_str());
    labels3dReady_ = !labelData_.All().empty() && labels3d_.Init();
    if (labels3dReady_ && textReady_) {
        labels3d_.Bind(&labelData_, &fonts_, &fontPages_);
        // Text, visibility and fades are read from each label's node from here on.
        const int unbound = labels3d_.BindTree(*tree_);
        if (unbound > 0) Log(Printf("label3d: %d labels have no node in the runtime scene", unbound));
    }

    // --- debug --------------------------------------------------------------
    screenPrefixes_ = {
        "Camera/dialogue UI/bracket ui",
        "Camera/dialogue UI/shooting selection",
        "Camera/dialogue UI/bracket ui/item grabbing",
        "Camera/dialogue UI/bracket ui/item stealing",
        "Camera/dialogue UI/item description",
        kDialogueScreen,
    };
    dbg_.buildId = config_.buildId;
    debug::GatherStatic(dbg_, room_, animLib_, *tree_,
                        uiReady_ ? &uiLayout_ : nullptr,
                        textReady_ ? &fonts_ : nullptr, screenPrefixes_);
    // The surface pages are on the GPU now; their CPU copy (up to the 33 MB
    // page budget) goes.
    room_.Textures().ReleasePixels();
    room_.ReleaseGeometry();   // on the GPU now; the renderer kept what it needs
    overlay_.SetDesignSize(uiLayout_.DesignWidth() > 0 ? uiLayout_.DesignWidth() : 960,
                           uiLayout_.DesignHeight() > 0 ? uiLayout_.DesignHeight() : 544);
    if (config_.startWithOverlay) {
        platform::FrameInput open{};
        open.toggleDebug = true;
        overlay_.Handle(open, dbg_);
    }

    // --- a scenario, or the session ------------------------------------------
    if (!config_.scenarioJson.empty()) {
        // The scenario seeds the round itself, at its start frame, as the
        // parity harness does: no session start, no menu.
        if (!runner_.Parse(config_.scenarioJson)) {
            Log("FATAL: --scenario has no wait_for_visible gate");
            return false;
        }
        scenarioActive_ = true;
        // BSHT_SHOT_HANDSHAKE (tools/vita3k/run.sh places it): each shot is held until
        // the grabber writes shot_<N>.done beside the saves, at most 20 s.
        shotHandshake_ = ReadDebugSwitch("BSHT_SHOT_HANDSHAKE", config_.saveDir).on;
        constexpr int kHandshakeMaxFrames = 1200;
        shotHold_ = ShotHold(config_.scenarioShotHoldFrames, shotHandshake_ ? kHandshakeMaxFrames : 0);
        Log("scenario: " + runner_.name() + (config_.scenarioShotHoldFrames > 0
            ? ", each shot held " + std::to_string(config_.scenarioShotHoldFrames) + " frames" : "")
            + (shotHandshake_ ? ", then until the grabber releases it (BSHT_SHOT_HANDSHAKE)" : ""));
        return true;
    }
    if (scene_.name == "main" && !StartGame()) return false;
    LogHeap(config_.log, "after init");
    return true;
}

// Where a streamed file is: flat with underscores in bsht-extract's game
// folder, else beside the batch tracks' parent ("audio/"), then the batch
// track folder itself ("audio/music/").
std::string GameApp::StreamPath(const std::string& stream) const {
    const std::string& dir = config_.musicDir.empty() ? config_.dataRoot : config_.musicDir;
    if (config_.musicSpaceFreeNames) {
        std::string flat = stream;
        for (char& c : flat) {
            if (c == ' ') c = '_';
        }
        return dir + "/" + flat;
    }
    const std::size_t slash = dir.find_last_of('/');
    const std::string parent = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
    const std::string first = parent + "/" + stream;
    if (std::FILE* f = std::fopen(first.c_str(), "rb")) {
        std::fclose(f);
        return first;
    }
    return dir + "/" + stream;
}

void GameApp::SyncStreams() {
    core::ProfScope prof(&profiler_, core::ProfSection::AudioSync);
    if (tree_ && tree_->free_generation() != streamFreeGeneration_) {
        // Something was freed: drop players that are gone, without reading them.
        streamFreeGeneration_ = tree_->free_generation();
        std::vector<const godot::AudioPlayback*> alive;
        for (godot::Node* n : tree_->nodes_in_tree_order()) {
            if (const godot::AudioPlayback* a = n->as_audio_playback()) alive.push_back(a);
        }
        streamRouter_.Prune([&alive](const godot::AudioPlayback* p) {
            return std::find(alive.begin(), alive.end(), p) != alive.end();
        });
    }
    for (const game::StreamRouter::Change& c : streamRouter_.Poll()) {
        switch (c.kind) {
        case game::StreamRouter::Kind::Start:
            Log(Printf("stream: voice %d %s%s", c.voice, c.path.c_str(), c.loop ? " (loop)" : ""));
            if (config_.playStream) config_.playStream(c.voice, c.path.c_str(), c.loop, c.gain.l, c.gain.r, c.pitch, c.musicBus);
            break;
        case game::StreamRouter::Kind::Stop:
            if (config_.stopStream) config_.stopStream(c.voice);
            break;
        case game::StreamRouter::Kind::Gain:
            if (config_.setStreamGain) config_.setStreamGain(c.voice, c.gain.l, c.gain.r);
            break;
        }
    }
}

// MusicManager chose a track or SpeakerController moved its volume: tell the
// platform's streamer. One call per change, never per frame.
void GameApp::SyncMusic() {
    core::ProfScope prof(&profiler_, core::ProfSection::AudioSync);
    const game::MusicRouter::Change c = musicRouter_.Poll();
    switch (c.kind) {
    case game::MusicRouter::Kind::Play:
        Log(Printf("music: %s at gain %.2f/%.2f (low-pass %.0f Hz, %.0f Hz on this device)", c.path.c_str(),
                   static_cast<double>(c.gain.l), static_cast<double>(c.gain.r), static_cast<double>(c.cutoffHz),
                   static_cast<double>(ScaledMusicCutoff(c.cutoffHz, config_.musicCutoffScale))));
        if (config_.playMusic) config_.playMusic(c.path.c_str(), c.gain.l, c.gain.r);
        if (config_.setMusicCutoff) config_.setMusicCutoff(ScaledMusicCutoff(c.cutoffHz, config_.musicCutoffScale));
        break;
    case game::MusicRouter::Kind::Adopt:
        Log(Printf("music: %s goes on from the loading screen at gain %.2f/%.2f", c.path.c_str(),
                   static_cast<double>(c.gain.l), static_cast<double>(c.gain.r)));
        if (config_.setMusicGain) config_.setMusicGain(c.gain.l, c.gain.r);
        if (config_.setMusicCutoff) config_.setMusicCutoff(ScaledMusicCutoff(c.cutoffHz, config_.musicCutoffScale));
        break;
    case game::MusicRouter::Kind::Cutoff:
        if (config_.setMusicCutoff) config_.setMusicCutoff(ScaledMusicCutoff(c.cutoffHz, config_.musicCutoffScale));
        break;
    case game::MusicRouter::Kind::Stop:
        Log("music: stopped");
        if (config_.playMusic) config_.playMusic("", 0.0f, 0.0f);
        break;
    case game::MusicRouter::Kind::Gain:
        if (config_.setMusicGain) config_.setMusicGain(c.gain.l, c.gain.r);
        // A fade moves the gain every frame; log only the ends of it, so the
        // report shows the ducking (ItemManager.GrabSpook) without 50 lines.
        if ((std::max(c.gain.l, c.gain.r) < 0.05f) != musicDucked_) {
            musicDucked_ = std::max(c.gain.l, c.gain.r) < 0.05f;
            Log(musicDucked_ ? "music: ducked" : "music: back up");
        }
        break;
    case game::MusicRouter::Kind::None:
        break;
    }
}

// The scene's current Camera3D, which CameraManager moves.
void GameApp::SyncCamera() {
    if (cameraOverride_) return;
    if (const godot::Camera3D* cam = tree_->get_current_camera()) {
        // camera_3d.cpp:166: the renderer sees the camera through
        // get_camera_transform, so CameraShaker's h_offset / v_offset move
        // the view without moving the node.
        camera_.SetFromGodot(cam->get_camera_transform(), cam->get_fov(), config_.sceneAspect, 0.1f,
                             config_.farPlane);
    }
}

// One device feature's verdict: the platform's default, then the two switches
// (an environment variable on the desktop, a file in the save directory on the
// Vita). Logged once, whatever it is, so every report says what ran.
FeatureVerdict GameApp::DecideDeviceFeature(DeviceFeature f) {
    const DeviceFeatureInfo& info = Info(f);
    const bool onByDefault = !config_.deviceDefaults || OnByDefaultOnDevice(f);
    const DebugSwitch off = ReadDebugSwitch(info.offSwitch, config_.saveDir);
    const DebugSwitch force = ReadDebugSwitch(info.forceSwitch, config_.saveDir);
    const FeatureVerdict v = DecideFeature(f, onByDefault, off.on, force.on);
    const std::string from = off.on ? " -- " + off.source : force.on ? " -- " + force.source : "";
    Log(std::string("device feature: ") + info.name + " " + v.why + from);
    return v;
}

// One scenario frame at a fixed 1/60 s: the runner's input and physics step,
// the tree's frame, and the shot the scenario asks for, in bsht_parity's
// order, so the frame drawn next is the state the checker compared.
void GameApp::RunScenarioFrame() {
    godot::Input& input = godot::Input::get_singleton();
    pendingShot_ = -1;
    if (shotHold_.Holding()) {   // the shot's frame, redrawn until it is read (app/shot_hold.h)
        const int held = shotHold_.Held();
        const bool released = !shotHandshake_ ||
            std::ifstream(config_.saveDir + "/shot_" + std::to_string(held) + ".done").good();
        pendingShot_ = shotHold_.Tick(released);
        if (pendingShot_ >= 0) {
            Log("scenario: " + std::string(shotHold_.TimedOut() ? "shot grab timed out " : "released shot ") +
                std::to_string(held));
        }
        return;
    }
    const bool running = runner_.BeginFrame(*tree_, input) == ScenarioRunner::Step::Running;
    const int at = running ? runner_.at() : -1;
    if (!runner_.EndFrame(input)) scenarioDone_ = true;
    tree_->process_frame(1.0 / 60.0);
    SyncDialogueBox();
    SyncCamera();
    SyncCursor();
    for (int shot : runner_.shots()) {
        if (!running || shot != at) continue;
        pendingShot_ = shotHold_.Request(at);
        // tools/vita3k/run.sh grabs the emulator's window when it sees this line.
        if (shotHold_.Holding()) Log("scenario: holding shot " + std::to_string(at));
    }
}

// Starts the session in main.tscn; RoundManager.BeginMainGame() runs inside
// Session::Start. Init and every change into main call it, so a boot into the
// menu starts nothing.
bool GameApp::StartGame() {
    session_.SetLog(config_.log);
    session_.SetStartBatch(config_.startBatch);
    session_.SetPlaysIntro(PlaysIntro(config_.skipMenu, !config_.scenarioJson.empty()));
    // Nothing is committed until the session actually starts: a failed start
    // must not swap the track first.
    if (!session_.Start(tree_.get())) {
        Log("FATAL: session failed to start");
        return false;
    }
    // roomMusicPath is the fallback for a scene with no MusicManager.
    if (config_.playMusic && !musicRouter_.Bound() && !config_.roomMusicPath.empty()) {
        config_.playMusic(config_.roomMusicPath.c_str(), 0.6f, 0.6f);
    }
    return true;
}

// A change failed after the old tree was destroyed (a file present but
// unreadable): nothing bound to the old tree may run again. Unbind what points
// into it, stop updating and drawing, and quit with the reason in the log.
void GameApp::AbandonScene() {
    sceneBroken_ = true;
    achEntry_ = nullptr;
    achScreen_.Cancel();
    uiReady_ = false;
    labels3dReady_ = false;
    Log("scene: FATAL -- the new scene could not load; quitting");
    if (config_.quit) config_.quit();
}

// SceneTree.change_scene_to_file, performed after the frame that asked for it:
// one scene resident at a time. The old tree (its scripts, pending timers and
// awaits) is destroyed first, then the renderer's room; then the next room is
// read behind the loading screen and the next tree is built and bound as Init
// builds the first. death.tscn is main (TempDeathScreen changes straight back).
// PlayerData crosses over in memory, as SaveFileManager's save/load pair
// carries it in the original.
void GameApp::ChangeScene(const std::string& path) {
    const SceneFiles next = FilesForScene(path, config_.dataRoot, config_.roomRoot, config_.portRoot);
    if (next.name.empty()) {
        Log("scene: change to " + path + " is not ported -- staying");
        return;
    }
    // Before anything is torn down: a scene whose files are not all readable
    // (a game folder copied in part) is refused, and the current one plays on.
    if (!SceneFilesPresent(next)) {
        Log("scene: " + path + " is missing files (" + next.room + ", " + next.runtime + ", ...) -- staying");
        return;
    }
    Log("scene: " + scene_.name + " -> " + path + (next.name == "main" && path != "res://scenes/main.tscn"
                                                      ? " (-> res://scenes/main.tscn)" : ""));

    if (config_.memoryReport) Log("scene: before the change, " + config_.memoryReport());
    LogHeap(config_.log, "before the change", /*resetPeak=*/true);
    // What the original reloads: the PlayerData SaveGame wrote in this scene
    // (a normal death saves; a death in Double or Nothing does not).
    scripts::PlayerData carried;
    bool haveCarried = false;
    if (auto* save = godot::script_cast<scripts::SaveFileManager>(
            tree_->get_node("standalone managers/save file manager"))) {
        if (save->savedThisScene) {
            carried = *save->savedThisScene;
            haveCarried = true;
        }
    }

    // Nothing may keep pointing into the old tree once it is gone.
    players_.clear();
    lastPlayed_ = nullptr;
    dialogueScript_ = nullptr;
    cameraManager_ = nullptr;
    autoplayStep_ = 0;
    autoplayDelay_ = 0.0f;
    musicRouter_ = game::MusicRouter();
    // No ambience bleeds into the next scene, and no banked sound either:
    // Godot frees every player, and a freed player stops.
    if (config_.stopAllVoices) config_.stopAllVoices();
    streamRouter_.Reset();
    SyncStreams();
    streamRouter_ = game::StreamRouter();
    tree_ = std::make_unique<godot::SceneTree>();
    tree_->set_profiler(&profiler_);
    // The old scene's .tres materials go with it, as Godot frees a resource
    // whose last scene is gone: a death in endless (main -> death -> main)
    // must not keep the endless rounds display. Nothing alive points into
    // the library now; the new scene's load refills it.
    godot::MaterialLibrary::reset();

    const bool newRoom = next.room != scene_.room;
    if (newRoom && !LoadSceneRoom(next)) {
        Log("scene: " + next.room + " did not load -- the change failed");
        AbandonScene();
        return;
    }
    const bool newInterface = next.ui != scene_.ui;
    const bool newLabels = next.labels != scene_.labels;
    scene_ = next;

    const godot::RuntimeSceneError err = godot::LoadRuntimeScene(scene_.runtime.c_str(), *tree_);
    if (err != godot::RuntimeSceneError::None) {
        Log(Printf("scene: %s: %s -- the change failed", scene_.runtime.c_str(),
                   godot::RuntimeSceneErrorText(err)));
        AbandonScene();
        return;
    }
    if (scene_.name == "menu") Log(PortBranding::Describe(PortBranding::Apply(*tree_)));
    renderer_.BindTree(*tree_);
    dynLights_.Bind(*tree_);
    envGrade_.Bind(*tree_);
    movingLight_.Bind(*tree_);
    BindParticles();
    BindAnimation();
    BindScripts();
    BindInput();
    if (newLabels) {
        labelData_ = assets::Labels3D();
        if (!scene_.labels.empty()) labelData_.LoadFromFile(scene_.labels.c_str());
        labels3dReady_ = !labelData_.All().empty() && (labels3d_.Bound() || labels3d_.Init());
        if (labels3dReady_ && textReady_) labels3d_.Bind(&labelData_, &fonts_, &fontPages_);
    }
    if (labels3dReady_ && textReady_) labels3d_.BindTree(*tree_);
    if (newInterface && uiReady_) {
        uiReady_ = LoadSceneInterface();
    } else {
        dialogueScreen_.BindTree(tree_.get());
        uiScreen_.BindTree(tree_.get());
    }

    if (haveCarried) {
        if (auto* rm = godot::script_cast<scripts::RoundManager>(
                tree_->get_node("standalone managers/round manager"))) {
            if (rm->playerData) *rm->playerData = carried;   // gd: SaveFileManager.LoadGame
        }
    }

    if (config_.memoryReport) Log("scene: after the change, " + config_.memoryReport());
    LogHeap(config_.log, "after the change");
    if (scene_.name != "main") return;
    SnapCameraHome();

    // The batch index came across with PlayerData; a --batch diagnostic must
    // not override it on the way back in.
    config_.startBatch = 0;
    if (!StartGame()) Log("scene: the reloaded round could not start");
}

bool GameApp::LoadSceneRoom(const SceneFiles& files) {
    // The old room leaves the GPU and memory before the next is read, so the
    // two never coexist.
    const unsigned long oldTris = static_cast<unsigned long>(renderer_.TriangleCount());
    renderer_.Upload(assets::Room());
    room_ = assets::Room();
    Log(Printf("scene: released the old room (%lu triangles)", oldTris));
    LogHeap(config_.log, "old room released");

    LoadingScreen loading;
    const bool showing = static_cast<bool>(config_.swap);
    if (showing) {
        // GameApp's resident atlas and pages: no second read and no second
        // upload (~6 MB of VRAM on top of the next room's pages).
        BeginLoadingWith(loading, fonts_, fontPages_, config_.title, config_.swap);
        Log(loading.FontReady() ? "scene: loading screen drawn with the resident font pages"
                                : "scene: loading screen has no text -- the resident atlas lacks its faces");
        loading.Show("room", 0.0f);
    }
    assets::Room next;
    const assets::RoomFileError err = assets::ReadRoomFile(
        files.room.c_str(), next, [&](float p) { if (showing) loading.ShowProgress(0.9f * p); });
    if (err != assets::RoomFileError::None) {
        Log(Printf("scene: %s: %s", files.room.c_str(), assets::RoomFileErrorText(err)));
        if (showing) loading.DestroyGl();
        return false;
    }
    LogHeap(config_.log, "room read");
    if (showing) loading.Show("textures", 0.95f);
    const bool uploaded = renderer_.Upload(next);
    LogHeap(config_.log, "room uploaded");
    room_ = std::move(next);
    room_.Textures().ReleasePixels();
    room_.ReleaseGeometry();   // on the GPU now; the renderer kept what it needs
    if (showing) loading.DestroyGl();
    Log(Printf("scene: %s up, %d batches, %d triangles, %d/%d pages", files.room.c_str(),
               renderer_.BatchCount(), renderer_.TriangleCount(), renderer_.TexturesUploaded(),
               renderer_.TexturesTotal()));
    return uploaded;
}

// Every AnimationPlayer gets its animations; the ones the export left playing
// (the dealer's head bob, the smoking man) resume where Godot had them. Their
// AUDIO tracks reach the mixer through the tree's sink.
void GameApp::BindAnimation() {
    if (!animLib_.LoadFromFile(scene_.animations.c_str())) {
        Log("anim: " + scene_.animations + " missing -- the room will be static");
        return;
    }
    const int bound = godot::BindAnimationLibrary(*tree_, animLib_);
    for (godot::Node* n : tree_->nodes_in_tree_order()) {
        if (auto* p = godot::cast_to<godot::AnimationPlayer>(n)) players_.push_back(p);
    }
    Log(Printf("anim: %d animations, %d/%lu players bound", animLib_.Stats().animations, bound,
               static_cast<unsigned long>(players_.size())));

    tree_->set_audio_sink([this](const godot::AudioPlayback& a, const godot::AudioPlayRequest& req) {
        // Godot's mix gain for this player: 2D against the screen centre, 3D
        // from the current camera (godot/audio_spatial.h).
        const godot::StereoGain gain = godot::OutputGain(a);
        // A play nobody can hear although its volume is up (a 3D speaker with
        // no camera, or beyond its max_distance) is bug 15's kind of silence:
        // named once. A track started at -inf dB to fade in is not one.
        if (gain.l == 0.0f && gain.r == 0.0f && a.get_volume_db() > -79.0f &&
            std::find(inaudible_.begin(), inaudible_.end(), a.get_stream()) == inaudible_.end()) {
            inaudible_.push_back(a.get_stream());
            Log("audio: " + a.get_stream() + " played at zero gain (no camera, or beyond max_distance)");
        }
        if (config_.playAt >= 0.0f) {
            Log(Printf("audio: %s gain %.3f/%.3f shelf %.3f from %.3f end-off %.3f %s", a.get_stream().c_str(), gain.l,
                       gain.r, gain.shelf, req.from, req.end_offset, req.ramp_in ? "ramped" : "polyphonic add"));
        }
        // Music streams instead (SyncMusic); the bank holds no track, and a
        // looping stream must not be ticked to a stop.
        if (musicRouter_.IsMusic(&a)) return godot::AudioPlayResult{musicRouter_.OnPlay(), -1};
        // A file the bank does not hold streams on a stream voice.
        // PORT: a stream starts at its head (play(from), start_offset unused).
        if (config_.isBanked && !config_.isBanked(a.get_stream().c_str())) {
            return godot::AudioPlayResult{streamRouter_.OnPlay(&a), -1};
        }
        int voice = -1;
        if (config_.playSound) {
            platform::SoundPlay play;
            play.name = a.get_stream().c_str();
            play.gainL = gain.l;
            play.gainR = gain.r;
            // An animation's sound plays at pitch 1 (AudioStreamPlaybackPolyphonic).
            play.pitch = req.polyphonic ? 1.0f : a.get_pitch_scale();
            play.positional = a.space() == godot::AudioSpace::Positional3D;
            play.fromSeconds = req.from;
            play.endOffsetSeconds = req.end_offset;
            play.rampIn = req.ramp_in;
            play.shelf = gain.shelf;
            play.shelfCutoffHz = gain.shelf_cutoff_hz;
            voice = config_.playSound(play);
        }
        // Godot's own length (at this pitch, less the trimmed head and tail),
        // so is_playing lasts as long as the sound.
        const assets::AudioStreamInfo* info = audioStreams_.Find(a.get_stream());
        const float pitch = req.polyphonic ? 1.0f : (a.get_pitch_scale() > 0.01f ? a.get_pitch_scale() : 0.01f);
        float length = 0.0f;
        if (info) {
            length = info->loop ? game::StreamRouter::kNeverEnds
                                : std::max(0.0f, info->length - req.from - req.end_offset) / pitch;
        }
        return godot::AudioPlayResult{length, voice};
    });
    tree_->set_audio_stop_sink([this](int voice) {
        if (config_.stopVoice) config_.stopVoice(voice);
    });
    tree_->set_audio_update_sink([this](int voice, godot::StereoGain gain, float pitch) {
        if (config_.updateVoice) config_.updateVoice(voice, gain.l, gain.r, pitch, gain.shelf);
    });
}

// Every Achievement script in the tree records into the local store, and every
// SaveFileManager saves through the SaveStore. Done before _ready:
// SaveFileManager._ready loads. A diagnostic boot (skipMenu) runs with no save
// file, so a frame grab never depends on an earlier session's save.
void GameApp::BindPersistence() {
    int bound = 0;
    game::SaveStore* saves = UsesSaveStore(config_.skipMenu, !config_.scenarioJson.empty()) ? saves_.get() : nullptr;
    // A scenario's unlock: as dump_state.gd writes the pills file into its
    // private user:// before main.tscn loads.
    if (!saves && !config_.scenarioJson.empty() &&
        core::json::ReadBoolTopLevel(config_.scenarioJson.data(),
                                     config_.scenarioJson.data() + config_.scenarioJson.size(), "pills", false)) {
        if (!scenarioSaves_) scenarioSaves_ = std::make_unique<game::SaveStore>(config_.saveDir + "/scenario");
        for (const char* entry : {scripts::SaveFileManager::savePath, scripts::SaveFileManager::savePath_stats,
                                  game::LocalBoard::kEntry}) {
            scenarioSaves_->Clear(entry);
        }
        scenarioSaves_->Save(scripts::Unlocker::savepath, game::SaveMap{{"total_amount_selected", int64_t{0}}});
        saves = scenarioSaves_.get();
    }
    std::vector<godot::Node*> stack{tree_->get_root()};
    while (!stack.empty()) {
        godot::Node* n = stack.back();
        stack.pop_back();
        if (!n) continue;
        if (auto* ach = godot::script_cast<scripts::Achievement>(n)) {
            ach->store = &achievements_;
            ++bound;
        }
        if (auto* save = godot::script_cast<scripts::SaveFileManager>(n)) {
            save->store = saves;
        }
        if (auto* options = godot::script_cast<scripts::OptionsManager>(n)) {
            options->store = saves;
        }
        if (auto* unlocker = godot::script_cast<scripts::Unlocker>(n)) {
            unlocker->store = saves;   // the Double or Nothing unlock ("pills")
        }
        if (auto* board = godot::script_cast<scripts::Board>(n)) {
            board->store = saves;      // the local Double or Nothing board ("board")
        }
        if (auto* ending = godot::script_cast<scripts::EndingManager>(n)) {
            ending->store = saves;     // the lifetime stats ("playerstats")
        }
        if (auto* exitManager = godot::script_cast<scripts::ExitManager>(n)) {
            exitManager->vitaPadNames = config_.vitaPadNames;
        }
        if (auto* reset = godot::script_cast<scripts::ResetManager>(n)) {
            reset->padResets = config_.padResetsRun;
            if (!config_.padResetsRun) Log("input: the pad's reset (Triangle) is off; R still resets the run");
        }
        for (int i = 0; i < n->get_child_count(); ++i) stack.push_back(n->get_child(i));
    }
    Log(Printf("ach: %d achievement manager(s) bound", bound));
}

// PORT: the one addition to the original's menu. A real Button under the main
// screen, below EXIT: the pad reaches it through Godot's own focus links
// (EXIT -> it -> START, where EXIT wrapped to START before), and a click or
// touch presses it through the GUI like any other button.
void GameApp::AddAchievementsEntry() {
    achEntry_ = nullptr;
    achScreen_.Cancel();
    if (scene_.name != "menu") return;
    godot::Button* entry = app::AddAchievementsEntry(*tree_);
    if (!entry) {
        Log("ach: the menu has no EXIT button -- no ACHIEVEMENTS entry");
        return;
    }
    const app::ScreenRect r = app::ScreenRectOf(*entry);
    const app::ScreenRect text = app::AchievementsEntryTextBox(*tree_);
    achScreen_.SetEntryText(r.x, text.y, text.h);
    Log(Printf("ach: the ACHIEVEMENTS entry at (%.0f, %.0f), one row below EXIT; its text centred in y %.0f h %.0f",
               r.x, r.y, text.y, text.h));
    entry->connect("pressed", [this] {
        achScreen_.Open();
        Log("ach: the ACHIEVEMENTS screen is open");
    });
    achEntry_ = entry;
}

// While the screen is open it takes the input, as the debug overlay does:
// up/down scroll, back/accept/a click close it and return focus to its entry.
bool GameApp::HandleAchievementsInput(const platform::FrameInput& in) {
    constexpr int kWheelUp = 4, kWheelDown = 5;   // MouseButton WHEEL_UP / WHEEL_DOWN
    if (!achScreen_.IsOpen()) return false;
    for (const godot::InputEventRef& ev : in.events) {
        if (!ev || !ev->is_pressed()) continue;
        // The wheel scrolls; it does not close the screen.
        const auto* mb = godot::event_cast<godot::InputEventMouseButton>(ev.get());
        const bool wheelDown = mb && mb->button_index == kWheelDown;
        const bool wheelUp = mb && mb->button_index == kWheelUp;
        if (ev->is_action_pressed("ui_down") || wheelDown) {
            achScreen_.MoveDown();
        } else if (ev->is_action_pressed("ui_up") || wheelUp) {
            achScreen_.MoveUp();
        } else if (ev->is_action_pressed("ui_cancel") || ev->is_action_pressed("ui_accept") || mb ||
                   godot::event_cast<godot::InputEventScreenTouch>(ev.get())) {
            achScreen_.Cancel();
            if (achEntry_) achEntry_->grab_focus();
            Log("ach: the ACHIEVEMENTS screen is closed");
            break;
        }
    }
    return true;
}

void GameApp::BindScripts() {
    // The locale survives a scene change, as Godot's TranslationServer does.
    godot::set_translation_root(config_.dataRoot);
    const std::string locale = godot::get_locale().empty() ? config_.locale : godot::get_locale();
    if (godot::set_locale(locale)) {
        Log("tr: locale " + godot::get_locale());
    } else if (godot::set_locale("EN")) {
        Log("tr: no table for locale '" + locale + "' -- EN");
    } else {
        Log("tr: dialogue_en.json missing -- every line will show its key");
    }
    // Before binding: ShellSpawner's `shellInstance` resolves to this scene.
    if (!godot::PackedScene::load("res://instances/shell.tscn",
                                  Join(config_.dataRoot, "runtime_shell.json").c_str())) {
        Log("scenes: runtime_shell.json missing -- no shells can be spawned");
    }
    // Before binding: ItemManager's ItemResource.instance resolves to these, by
    // the res:// path each records. The list is ItemManager's own.
    if (godot::Node* itemManager = tree_->get_node("standalone managers/item manager")) {
        int loaded = 0;
        const std::vector<std::string> itemScenes = scripts::ItemScenePaths(*itemManager);
        for (const std::string& res : itemScenes) {
            const std::string file = scripts::ItemSceneExportFile(res);
            if (godot::PackedScene::load(res, Join(config_.dataRoot, file.c_str()).c_str())) {
                ++loaded;
            } else {
                Log(Printf("scenes: %s missing -- that item can never be dealt", file.c_str()));
            }
        }
        Log(Printf("scenes: %d/%lu item scenes", loaded, static_cast<unsigned long>(itemScenes.size())));
    }
    LoadInputMapOnce();
    // Every stream's length and loop flag, once: engine data.
    if (audioStreams_.Size() == 0) {
        const bool ok = audioStreams_.LoadFromFile(Join(config_.dataRoot, "audio_streams.json").c_str());
        Log(Printf("audio: %lu stream lengths%s", static_cast<unsigned long>(audioStreams_.Size()),
                   ok ? "" : " -- audio_streams.json missing: long sounds stay silent"));
    }
    scripts::RegisterScripts();
    scripts_ = scripts::BindScripts(tree_->get_root());
    Log(Printf("scripts: %d attached, %d bound, %lu binding errors", scripts_.created, scripts_.bound,
               static_cast<unsigned long>(scripts_.errors.size())));
    for (const std::string& e : scripts_.errors) Log("scripts: " + e);
    BindPersistence();
    BindSceneAudio();
    tree_->ready_scripts();
    if (scene_.name == "menu") {
        // After ready: v2.2's MenuManager._ready writes the game's version.
        const std::string game = PortBranding::WriteVersion(*tree_);
        Log("menu: version label " + std::string(PortBranding::kVersion) + " (the game's: " + game + ")");
    }
    if (config_.hideDesktopOptions && HideDesktopOnlyOptions(*tree_)) Log("menu: desktop-only options are hidden (Vita)");
    AddAchievementsEntry();
    for (const std::string& e : scripts::InstanceBindErrors()) Log("scripts (instances): " + e);
    dialogueScript_ = godot::script_cast<scripts::Dialogue>(tree_->get_node("standalone managers/dialogue manager"));
    cameraManager_ = godot::script_cast<scripts::CameraManager>(tree_->get_node("standalone managers/camera manager"));
}

// The stream and music routers for the new tree, before any _ready: the menu
// plays its track in MenuManager._ready, and a play the routers do not know
// yet goes to the SFX path with no length and stops a frame later (the menu
// came back silent after a game).
void GameApp::BindSceneAudio() {
    streamRouter_ = game::StreamRouter(&audioStreams_, [this](const std::string& n) { return StreamPath(n); });
    streamFreeGeneration_ = tree_->free_generation();
    musicRouter_ = game::MusicRouter(config_.musicDir.empty() ? config_.dataRoot : config_.musicDir, "");
    musicRouter_.SetSpaceFreeNames(config_.musicSpaceFreeNames);
    // Only the first scene, the boot menu, can find its track already playing.
    const std::string startedTrack = std::exchange(config_.menuTrackPath, std::string());
    const SceneMusic music = FindSceneMusic(*tree_);
    if (!music.player) {
        Log("music: no MusicManager script -- the batch tracks will not change");
        return;
    }
    musicRouter_.Bind(music.player);
    if (music.cutoffHz) musicRouter_.BindFilter(music.cutoffHz);
    if (!startedTrack.empty() && music.owner == "MenuManager") musicRouter_.AdoptRunning(startedTrack);
    Log("music: " + music.owner + "'s player streams the music");
}

// The original's own bindings (project.godot [input] -> input_map.json), and
// every parsed event delivered through the scene tree as Viewport::push_input.
// The project's InputMap, loaded ONCE: it is engine state in Godot and
// survives scene changes, so a rebind or a saved map applied in the menu
// (OptionsManager) is still bound at the table. Loaded before any _ready:
// OptionsManager reads its defaults from it.
void GameApp::LoadInputMapOnce() {
    if (inputMapLoaded_) return;
    inputMapLoaded_ = true;
    godot::InputMap& map = godot::InputMap::get_singleton();
    if (!map.load(Join(config_.dataRoot, "input_map.json").c_str())) {
        Log("input: input_map.json missing -- no key or button does anything "
            "(run tools/godot/export_input_map.gd)");
    } else {
        Log(Printf("input: %lu actions", static_cast<unsigned long>(map.get_actions().size())));
    }
}

// AudioServer bus 0 (OptionsManager.ApplySettings_volume) to the mixer's
// master gain, once per change; the muted buses (music off) logged on a change.
void GameApp::SyncMasterVolume() {
    const godot::AudioServer& server = godot::AudioServer::get_singleton();
    if (server.mute_mask() != muteMask_) {
        muteMask_ = server.mute_mask();
        Log("audio: muted buses " + server.muted_text());
    }
    const float db = server.get_bus_volume_db(0);
    if (db == masterDb_) return;
    masterDb_ = db;
    if (config_.setMasterGain) config_.setMasterGain(static_cast<float>(godot::db_to_linear(db)));
    Log(Printf("audio: master %.2f dB", static_cast<double>(db)));
}

void GameApp::BindInput() {
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    input.set_dispatch([this](const godot::InputEventRef& ev) { tree_->push_input(ev); });
    // The velocity track counts the tree's time, so a run with scripted input repeats.
    input.set_ticks_usec([this] { return static_cast<uint64_t>(tree_->get_raw_time() * 1e6); });
}

void GameApp::SnapCameraHome() {
    if (!cameraManager_ || !cameraManager_->cam) {
        Log("camera: no CameraManager script -- the view stays where the export left it");
        return;
    }
    for (const scripts::CameraSocket* socket : cameraManager_->socketArray) {
        if (socket && socket->socketName == "home") {
            cameraManager_->cam->set_position(socket->pos);
            cameraManager_->cam->set_rotation_degrees(socket->rot);
            cameraManager_->cam->set_fov(static_cast<float>(socket->fov));
            return;
        }
    }
    Log("camera: no 'home' socket");
}

// PORT: the backdrop's scale is not drawn; DialogueManager only widens it for
// the FR/IT/DE/ES/BR/PT locales (ShellSpawner.CheckBackdropScaling).
void GameApp::SyncDialogueBox() {
    if (!dialogueReady_ || !dialogueScript_ || !dialogueScript_->dialogueUI) return;
    const godot::Label& label = *dialogueScript_->dialogueUI;
    dialogueScreen_.SetShown(dialogueText_, label.is_visible_in_tree());
    if (dialogueBackdrop_ >= 0 && dialogueScript_->dialogueUI_backdrop) {
        dialogueScreen_.SetShown(dialogueBackdrop_, dialogueScript_->dialogueUI_backdrop->is_visible_in_tree());
    }
    const std::string_view text = label.get_text();
    const int shown = label.get_visible_characters();
    dialogueScreen_.SetText(dialogueText_,
                            shown < 0 ? text : text.substr(0, godot::utf8_prefix_bytes(text, shown)));
}

bool GameApp::LoadInterface() {
    if (!sprites_.Init()) {
        Log("ui: sprite shader failed to compile");
        return false;
    }

    // A 1x1 white: flat ColorRect fills, and the overlay's scrim.
    const uint8_t white[4] = {255, 255, 255, 255};
    uiWhite_.Upload(white, 1, 1);

    // Glyphs come from Godot's own rasteriser, repacked into one page, so text
    // can never split a batch. The entry point read it before the room so the
    // loading screen could draw; this takes ownership of that same page.
    if (fonts_.Faces().empty()) {
        Log("fonts: atlas not loaded -- running without text");
    } else {
        fonts_.SetLog([this](const std::string& line) { Log(line); });
        fontPages_.clear();
        fontPages_.resize(fonts_.Pages().size());
        int uploaded = 0;
        int base = 0;
        for (std::size_t i = 0; i < fonts_.Pages().size(); ++i) {
            const assets::FontPage& p = fonts_.Pages()[i];
            if (p.group != "base") continue;
            ++base;
            if (!p.pixels.empty() &&
                fontPages_[i].Upload(p.pixels.data(), p.width, p.height,
                                     render::SpriteTexture::Format::LuminanceAlpha8)) {
                ++uploaded;
            }
        }
        textReady_ = uploaded > 0 && fontPages_[0].Valid();
        fonts_.ReleasePixels();   // on the GPU now; the CPU copy is 5 MB
        Log(Printf("fonts: %lu faces, %d/%d base pages uploaded, %lu pages in all",
                   static_cast<unsigned long>(fonts_.Faces().size()), uploaded, base,
                   static_cast<unsigned long>(fonts_.Pages().size())));
        if (!textReady_) Log("fonts: atlas failed to upload");
    }

    return LoadSceneInterface();
}

// The scene's own interface: its layout and textures, and the screens it
// opens. main opens the dialogue box and the shooting interface; the menu
// draws everything under its dialogue UI, and its tree decides what shows.
bool GameApp::LoadSceneInterface() {
    // The screens' member indices belong to the layout about to be replaced:
    // close them first, or they resolve against the next scene's smaller
    // layout.
    dialogueScreen_.Close();
    uiScreen_.Close();
    const assets::UiLayoutError e = uiLayout_.Load(scene_.ui.c_str(), scene_.uiPixels.c_str());
    if (e != assets::UiLayoutError::None) {
        Log(Printf("ui: %s: %s -- running without an interface", scene_.ui.c_str(),
                   assets::UiLayoutErrorText(e)));
        return false;
    }
    uiTextures_.clear();
    uiTextures_.resize(uiLayout_.Textures().size());
    int uploaded = 0;
    for (std::size_t i = 0; i < uiLayout_.Textures().size(); ++i) {
        const assets::UiTexture& t = uiLayout_.Textures()[i];
        const uint8_t* px = uiLayout_.TexturePixels(static_cast<int>(i));
        if (px && uiTextures_[i].Upload(px, t.width, t.height)) ++uploaded;
    }
    uiLayout_.ReleasePixels();   // on the GPU now (the menu's pool alone is 17.6 MB)
    Log(Printf("ui: %d controls, %d/%lu textures uploaded",
               static_cast<int>(uiLayout_.Controls().size()), uploaded,
               static_cast<unsigned long>(uiLayout_.Textures().size())));
    if (uploaded == 0) return false;
    LoadSceneLogo();
    if (scene_.name == "menu") {
        // The port's credit closes the main credits: what follows moves down by its lines.
        const float room = PortBranding::CreditHeight(fonts_, uiLayout_);
        const int moved = PortBranding::MakeRoomForTheCredit(*tree_, room);
        Log(Printf("menu: room for the port's credit: %d credits nodes moved down %.1f px", moved,
                   static_cast<double>(room)));
    }

    // The tree decides what is on screen (ui_screen.h); bind before opening.
    dialogueScreen_.BindTree(tree_.get());
    uiScreen_.BindTree(tree_.get());
    uiScreen_.SetLog([this](const std::string& line) { Log(line); });
    faceOfControl_.clear();
    if (scene_.name != "main") {
        dialogueScreen_.Close();
        dialogueReady_ = false;
        dialogueText_ = dialogueBackdrop_ = -1;
        if (!uiScreen_.Open("Camera/dialogue UI")) {
            Log("ui: " + scene_.name + " has no dialogue UI");
            return false;
        }
        return true;
    }
    // The dialogue box is its own screen: open beside whatever the player is
    // choosing between, with nothing to focus.
    dialogueText_ = uiLayout_.Find(std::string(kDialogueScreen) + "/dialogue text");
    dialogueBackdrop_ = uiLayout_.Find(std::string(kDialogueScreen) + "/dialogue backdrop");
    dialogueReady_ = dialogueText_ >= 0 && dialogueScreen_.Open(kDialogueScreen);

    // Every subtree of the dialogue UI but the dialogue box (app/ui_screens.h);
    // the tree's visibility and modulate decide what shows, as in Godot.
    const std::vector<std::string> screens = TableUiScreens(uiLayout_, kDialogueScreen);
    const bool opened = config_.uiScreenPrefix ? uiScreen_.Open(config_.uiScreenPrefix) : uiScreen_.OpenAll(screens);
    if (!opened) {
        Log("ui: the table's screens matched no controls");
        return false;
    }
    Log(Printf("ui: the table draws %d subtrees of the dialogue UI", static_cast<int>(screens.size())));
    return true;
}

godot::AnimationPlayer* GameApp::PlayOn(const std::string& player, const std::string& animation) {
    auto* p = tree_->get_node_as<godot::AnimationPlayer>(player);
    if (!p) return nullptr;
    p->play(animation);
    lastPlayed_ = p;
    return p;
}

void GameApp::HandleDebug(const platform::FrameInput& in) {
    const bool wasVisible = overlay_.Visible();
    const debug::OverlayAction action = overlay_.Handle(in, dbg_);
    if (overlay_.Visible() != wasVisible) {
        debug::LogOverlayToggle(config_.log, overlay_.Visible(), overlay_.CurrentPage());
    }
    if (action.kind == debug::OverlayAction::Kind::None) return;

    bool applied = false;
    switch (action.kind) {
        case debug::OverlayAction::Kind::PlayAnimation:
            applied = PlayOn(action.playerPath, action.name) != nullptr;
            break;
        case debug::OverlayAction::Kind::TogglePropOverride:
            applied = renderer_.OverridePropVisible(action.nodePath, action.visible);
            break;
        case debug::OverlayAction::Kind::OpenScreen:
            if (uiReady_ && action.screen >= 0 &&
                action.screen < static_cast<int>(screenPrefixes_.size())) {
                applied = uiScreen_.Open(screenPrefixes_[static_cast<std::size_t>(action.screen)]);
            }
            break;
        case debug::OverlayAction::Kind::None:
            break;
    }
    debug::LogAction(config_.log, action, dbg_, applied);
}

void GameApp::RunPlayAt(float dt) {
    if (config_.playAt < 0.0f) return;
    playClock_ += dt;
    for (ChainedPlay& c : playChain_) {
        if (c.fired || playClock_ < c.at) continue;
        c.fired = true;
        const bool found = PlayOn(c.player, c.animation) != nullptr;
        Log(Printf("play-at %.2fs: %s | %s -> %s", playClock_, c.player.c_str(), c.animation.c_str(),
                   found ? "playing" : "no such player"));
    }
    if (playFired_ || playClock_ < config_.playAt) return;
    playFired_ = true;
    // "anim;P2|anim2@delay;...": the first entry is this player's, the rest
    // are chained plays timed from now.
    std::string first = config_.playAnimation;
    const std::size_t semi = first.find(';');
    if (semi != std::string::npos) {
        std::string rest = first.substr(semi + 1);
        first.resize(semi);
        while (!rest.empty()) {
            const std::size_t next = rest.find(';');
            const std::string item = rest.substr(0, next);
            rest = next == std::string::npos ? std::string{} : rest.substr(next + 1);
            const std::size_t bar = item.find('|');
            const std::size_t atSign = item.rfind('@');
            if (bar == std::string::npos || atSign == std::string::npos || atSign < bar) continue;
            playChain_.push_back({item.substr(0, bar), item.substr(bar + 1, atSign - bar - 1),
                                  playClock_ + static_cast<float>(std::atof(item.c_str() + atSign + 1)), false});
        }
    }
    const bool found = PlayOn(config_.playPlayer, first) != nullptr;
    Log(Printf("play-at %.2fs: %s | %s -> %s", playClock_, config_.playPlayer.c_str(), first.c_str(),
               found ? "playing" : "no such player"));
}

namespace {

// A joypad button event, as platform/ raises the Vita's pad.
godot::InputEventRef PadEvent(int button, bool pressed) {
    auto e = std::make_shared<godot::InputEventJoypadButton>();
    e->button_index = button;
    e->pressed = pressed;
    e->device = 0;
    return e;
}

}  // namespace

void ApplyDataLayout(AppConfig& config, const DataLayout& layout) {
    config.dataRoot = layout.data;
    config.roomRoot = layout.rooms;
    config.portRoot = layout.port;
    config.achievementsPath = layout.achievements;
    config.roomMusicPath = layout.roomMusic;
    config.musicDir = layout.musicDir;
    config.musicSpaceFreeNames = layout.spaceFreeMusic;
}

std::vector<AppConfig::AutoInput> ParseAutoplayInput(const std::string& spec, std::string& error) {
    // "T:pad:down" presses the Vita pad's button through the joypad path (the
    // first d-pad press crashed on hardware and keys could not reach it).
    static const std::pair<const char*, int> kPads[] = {
        {"up", godot::joy::DPAD_UP}, {"down", godot::joy::DPAD_DOWN}, {"left", godot::joy::DPAD_LEFT},
        {"right", godot::joy::DPAD_RIGHT}, {"cross", godot::joy::A}, {"circle", godot::joy::B},
        {"start", godot::joy::START}, {"select", godot::joy::BACK}};
    static const std::pair<const char*, std::int64_t> kKeys[] = {
        {"up", godot::key::UP}, {"down", godot::key::DOWN}, {"left", godot::key::LEFT},
        {"right", godot::key::RIGHT}, {"enter", godot::key::ENTER}, {"escape", godot::key::ESCAPE}};
    std::vector<AppConfig::AutoInput> out;
    // A pointer entry carries its own comma ("T:move:480,270"), so entries are
    // split on the comma that follows a complete one.
    std::size_t start = 0;
    while (start < spec.size()) {
        std::size_t end = spec.find(',', start);
        std::string entry = spec.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (entry.find(":move:") != std::string::npos || entry.find(":click:") != std::string::npos) {
            const std::size_t second = end == std::string::npos ? std::string::npos : spec.find(',', end + 1);
            entry = spec.substr(start, second == std::string::npos ? std::string::npos : second - start);
            end = second;
        }
        start = end == std::string::npos ? spec.size() : end + 1;
        if (entry.empty()) continue;

        char name[16] = {};
        float at = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        if (std::sscanf(entry.c_str(), "%f:pad:%15s", &at, name) == 2) {
            bool known = false;
            for (const auto& [n, b] : kPads) {
                if (std::string_view(name) == n) {
                    out.push_back({at, AppConfig::AutoInput::Kind::Pad, b, 0.0f, 0.0f});
                    known = true;
                }
            }
            if (known) continue;
        } else if (std::sscanf(entry.c_str(), "%f:key:%15s", &at, name) == 2) {
            std::int64_t code = 0;
            for (const auto& [n, c] : kKeys) {
                if (std::string_view(name) == n) code = c;
            }
            if (code != 0) {
                out.push_back({at, AppConfig::AutoInput::Kind::Key, code, 0.0f, 0.0f});
                continue;
            }
        } else if (std::sscanf(entry.c_str(), "%f:move:%f,%f", &at, &x, &y) == 3) {
            out.push_back({at, AppConfig::AutoInput::Kind::Move, 0, x, y});
            continue;
        } else if (std::sscanf(entry.c_str(), "%f:click:%f,%f", &at, &x, &y) == 3) {
            out.push_back({at, AppConfig::AutoInput::Kind::Click, 0, x, y});
            continue;
        }
        error += (error.empty() ? "" : ", ") + entry;
    }
    return out;
}

// --autoplay: plays itself through the real input path, with the keys
// tests/parity/scenarios/grab_shotgun_b1r1.json uses: ui_down and ui_right to
// reach the shotgun's bracket, ui_accept to pick it up, then ui_up and
// ui_accept to shoot the dealer. BaseButton presses on release, so each key is
// released the frame after it is pressed.
void GameApp::RunAutoplay(float dt) {
    if (!config_.autoplay) return;
    if (autoplayDelay_ > 0.0f) {
        autoplayDelay_ -= dt;
        return;
    }
    const auto* choice = tree_->get_node_as<godot::Control>(
        "Camera/dialogue UI/bracket ui/shooting choice");
    const auto* gunBracket = tree_->get_node_as<godot::Control>(
        "Camera/dialogue UI/bracket ui/tabletop/m button_shotgun");
    const bool choiceOpen = choice && choice->is_visible_in_tree();
    const bool canGrab = gunBracket && gunBracket->is_visible_in_tree();

    // The choice is up: aim at the dealer (or, --autoplay-self, the player) and fire.
    if (choiceOpen) {
        const bool self = config_.autoplaySelf;
        Push(autoplayStep_ == 0 ? (self ? godot::key::DOWN : godot::key::UP) : godot::key::ENTER,
             autoplayStep_ == 0 ? (self ? "ui_down" : "ui_up") : "ui_accept");
        autoplayStep_ = autoplayStep_ == 0 ? 1 : 0;
        autoplayDelay_ = 0.4f;
        return;
    }
    // Not the player's turn yet, or the gun is already in hand.
    if (!canGrab) {
        autoplayStep_ = 0;
        autoplayDelay_ = 0.2f;
        return;
    }
    // The player's turn: walk the focus onto the shotgun and take it.
    switch (autoplayStep_) {
    case 0: Push(godot::key::DOWN, "ui_down"); break;
    case 1: Push(godot::key::RIGHT, "ui_right"); break;
    default: Push(godot::key::ENTER, "ui_accept"); break;
    }
    autoplayStep_ = autoplayStep_ >= 2 ? 0 : autoplayStep_ + 1;
    autoplayDelay_ = 0.35f;
}

// One press, released next frame, logged with the action it stands for.
void GameApp::Push(std::int64_t code, const char* what) {
    godot::Input::get_singleton().parse_input_event(KeyEvent(code, true));
    releaseNextFrame_.push_back(code);
    Log(Printf("autoplay: %s (frame %lu)", what, static_cast<unsigned long>(tree_->get_frame())));
}

// Releases what either autoplay driver pressed last frame. BaseButton presses
// on release (base_button.cpp on_action_event), so a key never released
// presses nothing. It runs before either driver's early return for that
// reason.
void GameApp::ReleasePressedKeys() {
    if (releaseNextFrame_.empty()) return;
    godot::Input& input = godot::Input::get_singleton();
    // A negative code is a pad button: -(button + 1).
    for (std::int64_t code : releaseNextFrame_) {
        input.parse_input_event(code < 0 ? PadEvent(static_cast<int>(-code - 1), false) : KeyEvent(code, false));
    }
    releaseNextFrame_.clear();
}

void GameApp::RunAutoplayInput(float dt) {
    if (config_.autoplayInput.empty()) return;
    godot::Input& input = godot::Input::get_singleton();
    const auto clickEvent = [](float x, float y, bool pressed) {
        auto b = std::make_shared<godot::InputEventMouseButton>();
        b->button_index = godot::mouse::LEFT;
        b->pressed = pressed;
        b->position = {x, y};
        b->global_position = b->position;
        b->button_mask = pressed ? godot::mouse::MASK_LEFT : 0;
        b->device = 0;
        return b;
    };

    // (the releases are drained by ReleasePressedKeys, before this runs)
    if (releaseClickNextFrame_) {
        releaseClickNextFrame_ = false;
        input.parse_input_event(clickEvent(lastPointer_.x, lastPointer_.y, false));
    }
    inputClock_ += dt;
    while (nextAutoInput_ < config_.autoplayInput.size() &&
           inputClock_ >= config_.autoplayInput[nextAutoInput_].at) {
        const AppConfig::AutoInput& a = config_.autoplayInput[nextAutoInput_++];
        switch (a.kind) {
        case AppConfig::AutoInput::Kind::Key:
            input.parse_input_event(KeyEvent(a.keycode, true));
            releaseNextFrame_.push_back(a.keycode);
            Log(Printf("autoplay-input %.2fs: key %lld (frame %lu)", inputClock_,
                       static_cast<long long>(a.keycode), static_cast<unsigned long>(tree_->get_frame())));
            break;
        case AppConfig::AutoInput::Kind::Pad:
            input.parse_input_event(PadEvent(static_cast<int>(a.keycode), true));
            releaseNextFrame_.push_back(-a.keycode - 1);
            Log(Printf("autoplay-input %.2fs: pad %lld (frame %lu)", inputClock_,
                       static_cast<long long>(a.keycode), static_cast<unsigned long>(tree_->get_frame())));
            break;
        case AppConfig::AutoInput::Kind::Move: {
            auto m = std::make_shared<godot::InputEventMouseMotion>();
            m->position = {a.x, a.y};
            m->global_position = m->position;
            m->relative = {a.x - lastPointer_.x, a.y - lastPointer_.y};
            m->velocity = input.get_last_mouse_velocity();
            m->device = 0;
            input.parse_input_event(m);
            lastPointer_ = {a.x, a.y};
            Log(Printf("autoplay-input %.2fs: move %.0f,%.0f (frame %lu)", inputClock_, a.x, a.y,
                       static_cast<unsigned long>(tree_->get_frame())));
            break;
        }
        case AppConfig::AutoInput::Kind::Click:
            lastPointer_ = {a.x, a.y};
            input.parse_input_event(clickEvent(a.x, a.y, true));
            releaseClickNextFrame_ = true;
            Log(Printf("autoplay-input %.2fs: click %.0f,%.0f (frame %lu)", inputClock_, a.x, a.y,
                       static_cast<unsigned long>(tree_->get_frame())));
            break;
        }
    }
}

// CursorManager writes both of these on godot::Input; the platform draws them.
void GameApp::SyncCursor() {
    godot::Input& input = godot::Input::get_singleton();
    const int mode = static_cast<int>(input.get_mouse_mode());
    if (mode != lastMouseMode_) {
        lastMouseMode_ = mode;
        if (config_.showCursor) config_.showCursor(mode == static_cast<int>(godot::Input::MouseMode::Visible));
    }
    const std::string& image = input.custom_cursor_image();
    if (!image.empty() && image != lastCursorImage_) {
        lastCursorImage_ = image;
        // The OS draws the cursor, so it is never in a --shot frame: this line
        // is the only evidence that CursorManager's swap reached the screen.
        Log("cursor: " + image);
        if (config_.setCursor) config_.setCursor(image, input.custom_cursor_hotspot());
    }
}

// The previous frame (its update and its draw) joins the window; every
// kProfilerWindow frames the window is logged and starts again.
void GameApp::FoldProfilerFrame() {
    profiler_.EndFrame();
    if (profiler_.Frames() < kProfilerWindow) return;
    Log(profiler_.Format());
    profiler_.ResetWindow();
    // The heap's trend through play: a leak shows as a live figure that climbs
    // window after window.
    LogHeap(config_.log, "window", /*resetPeak=*/true);
    // The last frame's props, sampled with each window (render::SceneRenderer::PropCensus).
    const render::SceneRenderer::PropCensus& c = renderer_.Census();
    Log(Printf("props: drawn %d, still %d, mergeable %d over %d pages; variants %d, scaled %d, blended %d, "
               "skinned %d, moving light %d, clones %d; batched %d in %d draws, %d list rebuilds",
               c.drawn, c.still, c.mergeable, c.mergeablePages, c.variants, c.scaled, c.blended, c.skinned,
               c.moveScope, c.clones, c.batched, c.batchedDraws, c.listRebuilds));
}

void GameApp::Update(float dt, const platform::FrameInput& in) {
    FoldProfilerFrame();
    core::ProfScope prof(&profiler_, core::ProfSection::Update);
    if (sceneBroken_) return;
    if (soak_) {
        if (const std::string next = soak_->Next(dt, scene_.name); !next.empty()) tree_->change_scene_to_file(next);
        if (soak_->Done()) {
            Log("soak: done");
            soak_.reset();
            if (config_.quit) config_.quit();
        }
    }
    // --menu-accept: a real ui_accept on the menu's first focus, through the
    // GUI path the pad and keyboard use.
    if (menuAcceptIn_ > 0.0f && scene_.name == "menu" && (menuAcceptIn_ -= dt) <= 0.0f) {
        menuAcceptIn_ = -1.0f;
        ScriptMenuAccept();
    }
    if (menuAcceptPending_ && scene_.name == "menu") {
        menuAcceptPending_ = false;
        if (auto* menu = godot::script_cast<scripts::MenuManager>(
                tree_->get_node("standalone managers/menu manager"))) {
            if (menu->firstFocus_main) menu->firstFocus_main->grab_focus();
        }
        Push(godot::key::ENTER, "ui_accept (--menu-accept)");
    }

    if (scenarioActive_) {
        RunScenarioFrame();
        // Heard as in play; the scenario's state never depends on it.
        SyncMusic();
        SyncStreams();
        return;
    }
    HandleDebug(in);

    // Input reaches the game the way Godot's main loop delivers it: parsed into
    // godot::Input (buffered, motion accumulated), flushed before the frame's
    // process step (DisplayServer::process_events, main.cpp:3426), and dispatched
    // through SceneTree::push_input. The open debug overlay keeps it all.
    godot::Input& input = godot::Input::get_singleton();
    if (!overlay_.Visible() && !HandleAchievementsInput(in)) {
        for (const godot::InputEventRef& ev : in.events) input.parse_input_event(ev);
    }
    if (!config_.debugUnlock.empty()) {
        const std::size_t at = config_.debugUnlock.find('@');
        debugClock_ += dt;
        if (at == std::string::npos || debugClock_ >= std::atof(config_.debugUnlock.c_str() + at + 1)) {
            achievements_.Unlock(config_.debugUnlock.substr(0, at));
            config_.debugUnlock.clear();
        }
    }
    toast_.Update(dt, achievements_);
    ReleasePressedKeys();
    RunAutoplayInput(dt);
    RunAutoplay(dt);
    input.flush_buffered_events();

    RunPlayAt(dt);

    // One Godot frame: every playing AnimationPlayer advances, its METHOD keys
    // run, and the translated scripts process, in Godot's order.
    tree_->process_frame(dt);
    // Godot swaps the scene at the end of the frame that asked for it, and
    // quits there (MenuManager.Exit).
    SyncMasterVolume();
    if (tree_->take_quit()) {
        Log("scene: quit requested");
        if (config_.quit) config_.quit();
    }
    if (const std::string next = tree_->take_scene_change(); !next.empty()) ChangeScene(next);
    if (sceneBroken_) return;

    SyncMusic();
    SyncStreams();

    // DialogueManager decides what is said; this only puts its Label on screen.
    SyncDialogueBox();

    if (config_.debugRayX >= 0.0f) {
        std::string hit = "none";
        if (const godot::Camera3D* cam = tree_->get_current_camera()) {
            const godot::Vector2 at{config_.debugRayX, config_.debugRayY};
            godot::RayQuery q;
            q.from = godot::project_ray_origin(*cam, at, godot::kViewportSize);
            q.to = godot::project_position(*cam, at, 20000.0f, godot::kViewportSize);
            godot::RayResult r;
            if (tree_->physics_space().intersect_ray(q, r) && r.collider) hit = r.collider->get_path();
        }
        const bool heartbeat = tree_->get_frame() % 60 == 0;
        if (hit != lastDebugRayHit_ || heartbeat) {
            lastDebugRayHit_ = hit;
            godot::Vector3 eye;
            if (const godot::Camera3D* cam = tree_->get_current_camera()) {
                const core::Mat4 g = cam->get_camera_transform();   // camera_3d.cpp:166
                eye = {g.m[12], g.m[13], g.m[14]};
            }
            Log(Printf("ray %.0f,%.0f f%lu cam(%.2f,%.2f,%.2f) -> %s", config_.debugRayX, config_.debugRayY,
                       static_cast<unsigned long>(tree_->get_frame()), eye.x, eye.y, eye.z, hit.c_str()));
        }
    }

    // Diagnostic, with --debug-ray or --autoplay-input: what the interaction
    // scripts decided, logged when it changes.
    if (config_.debugRayX >= 0.0f || !config_.autoplayInput.empty()) {
        const godot::Control* focus = tree_->gui_get_focus_owner();
        const auto* cursor = godot::script_cast<scripts::CursorManager>(tree_->get_node("standalone managers/cursor manager"));
        const auto* interaction =
            godot::script_cast<scripts::InteractionManager>(tree_->get_node("standalone managers/interaction manager"));
        std::string line = "interaction: focus " + (focus ? focus->get_path() : std::string("none"));
        if (cursor) line += cursor->controller_active ? " controller" : " mouse";
        line += " branch ";
        line += interaction && interaction->activeInteractionBranch ? interaction->activeInteractionBranch->interactionAlias
                                                                    : std::string("none");
        if (line != lastInteractionLog_) {
            lastInteractionLog_ = line;
            Log(Printf("%s (frame %lu)", line.c_str(), static_cast<unsigned long>(tree_->get_frame())));
        }
    }

    SyncCamera();
    SyncCursor();

    dbg_.frame.frameMs = dt * 1000.0f;
    dbg_.frame.fps = dt > 0.0f ? 1.0f / dt : 0.0f;
    if (dbg_.frame.frameMs > dbg_.frame.worstMs) dbg_.frame.worstMs = dbg_.frame.frameMs;
    if (overlay_.Visible()) RefreshTargetStats();
}

// The census fields keep their names; with the animator gone they count
// AnimationPlayers: moving = playing now, ever = has played this run.
void GameApp::RefreshTargetStats() {
    int moving = 0;
    int ever = 0;
    for (const godot::AnimationPlayer* p : players_) {
        if (p->is_playing()) ++moving;
        if (p->has_played()) ++ever;
    }
    dbg_.frame.movingTargets = moving;
    dbg_.frame.everAnimated = ever;
}

const render::SpriteTexture& GameApp::FontPage(int page) const {
    static const render::SpriteTexture kNone;
    if (page < 0 || page >= static_cast<int>(fontPages_.size())) return kNone;
    return fontPages_[static_cast<std::size_t>(page)];
}

// A CJK locale's glyph pages are uploaded while it is chosen and released when
// it is left, so a player who never picks one pays nothing.
void GameApp::SyncFontGroup() {
    if (!textReady_) return;
    const std::string locale = godot::get_locale();
    if (locale == fontLocale_) return;
    fontLocale_ = locale;
    bool hasGroup = false;
    for (const assets::FontPage& p : fonts_.Pages()) hasGroup = hasGroup || p.group == locale;
    const std::string want = hasGroup ? locale : std::string();
    if (want == fontGroup_) return;
    for (std::size_t i = 0; i < fonts_.Pages().size(); ++i) {
        if (!fontGroup_.empty() && fonts_.Pages()[i].group == fontGroup_) fontPages_[i] = render::SpriteTexture();
    }
    fonts_.UnloadGroup();
    fontGroup_.clear();
    if (!want.empty()) {
        if (!fonts_.LoadGroup(want)) {
            Log("fonts: glyph pages for " + want + " missing -- its characters will not draw");
        } else {
            int pages = 0;
            unsigned long bytes = 0;
            for (std::size_t i = 0; i < fonts_.Pages().size(); ++i) {
                const assets::FontPage& p = fonts_.Pages()[i];
                if (p.group != want) continue;
                if (fontPages_[i].Upload(p.pixels.data(), p.width, p.height,
                                         render::SpriteTexture::Format::LuminanceAlpha8)) {
                    ++pages;
                    bytes += static_cast<unsigned long>(p.pixels.size());
                }
            }
            fontGroup_ = want;
            fonts_.ReleasePixels();
            Log(Printf("fonts: %s glyph pages up (%d, %.2f MB)", want.c_str(), pages,
                       static_cast<double>(bytes) / 1048576.0));
        }
    }
    if (labels3dReady_ && labels3d_.Bound()) labels3d_.Invalidate();
}

// The smoke. The texture is read once, the systems on every scene.
void GameApp::BindParticles() {
    if (!particlesTried_) {
        particlesTried_ = true;
        particlesReady_ = particles_.Init(Join(config_.roomRoot, "particle_smoke.rgba"));
    }
    if (!particlesReady_) return;
    particles_.Bind(*tree_, &room_.Probes());
    particleFreeGeneration_ = tree_->free_generation();
}

void GameApp::DrawScene() {
    if (sceneBroken_) return;
    core::ProfScope prof(&profiler_, core::ProfSection::Draw);
    SyncFontGroup();

    float dyn[4][3];
    bool dynLit = dynLights_.Uniforms(kDynGain, dyn);
    // Probe variant 1: all four animated lights burning at once, the DYN
    // program's worst case; 2: textures off; 3: props off.
    if (probeVariant_ == 1) {
        const float white[3] = {1.0f, 1.0f, 1.0f};
        for (auto& d : dyn) assets::light::DynUniform(white, 10.0f, kDynGain, d);
        dynLit = true;
    }
    renderer_.SetProbeVariant(probeVariant_ == 2, probeVariant_ == 3);
    renderer_.SetDynLights(dyn, dynLit);
    float envScale = 1.0f, envOffset = 0.0f;
    envGrade_.Uniforms(&envScale, &envOffset);
    renderer_.SetEnvironmentGrade(envScale, envOffset);
    float liveBrightness = 0.0f, liveContrast = 0.0f;
    const bool liveGrade = envGrade_.Live(&liveBrightness, &liveContrast);
    renderer_.SetLiveGrade(liveGrade, liveBrightness, liveContrast);
    EnvironmentGrade::Fog fog;
    const bool fogOn = envGrade_.FogUniforms(&fog);
    float movePos[3], moveRange = 1.0f, moveColour[3];
    const bool moveOn = movingLight_.Uniforms(camera_.Eye(), kDynGain, movePos, &moveRange, moveColour);
    renderer_.SetMovingLight(moveOn, movePos, moveRange, moveColour);
    renderer_.SetFog(fogOn, fogOn && !assets::FogIsBaked(scene_.name), fog.density, fog.colour, fog.clear, fog.mix);
    if (static_cast<int>(fogOn) != fogLogged_) {
        fogLogged_ = static_cast<int>(fogOn);
        char line[240];
        std::snprintf(line, sizeof line,
                      "fog: %s, density %.4f, colour (%.3f %.3f %.3f), merged batches %s; mix: 1/white %.3f, "
                      "b %.2f, c %.2f, fog luminance %.3f grey %.3f",
                      fogOn ? "on" : "off", static_cast<double>(fog.density), static_cast<double>(fog.colour[0]),
                      static_cast<double>(fog.colour[1]), static_cast<double>(fog.colour[2]),
                      assets::FogIsBaked(scene_.name) ? "baked" : "at runtime",
                      static_cast<double>(fog.mix.curve.whiteInv), static_cast<double>(fog.mix.curve.brightness),
                      static_cast<double>(fog.mix.curve.contrast), static_cast<double>(fog.mix.luminance),
                      static_cast<double>(fog.mix.grey));
        Log(line);
    }
    if (envGrade_.SaturationMoved() && !saturationReported_) {
        saturationReported_ = true;
        Log("PORT: adjustment_saturation moved; the bake graded it at rest and it is not applied");
    }
    {
        core::ProfScope props(&profiler_, core::ProfSection::Props);
        renderer_.Draw(camera_);
    }
    if (particlesReady_) {
        core::ProfScope parts(&profiler_, core::ProfSection::Particles);
        if (tree_->free_generation() != particleFreeGeneration_) {
            particleFreeGeneration_ = tree_->free_generation();
            particles_.Rebind(*tree_);
        }
        assets::light::PointLight live[4];
        const int liveCount = dynLights_.Lights(live);
        if (liveCount != dynLogged_) {   // the animated lights, when the count changes
            dynLogged_ = liveCount;
            Log("dyn lights: " + (liveCount ? dynLights_.Burning() : std::string("none")));
        }
        particles_.Draw(camera_.View(), camera_.ViewProj(), live, liveCount);
        // Logged whenever the set of live systems changes, with each count.
        if (particles_.LiveMask() != particlesLive_) {
            particlesLive_ = particles_.LiveMask();
            Log(Printf("particles: live now (%d drawn): %s", particles_.DrawnQuads(),
                       particlesLive_ ? particles_.LiveSummary().c_str() : "none"));
        }
    }
    if (labels3dReady_ && labels3d_.Bound()) {
        core::ProfScope labels(&profiler_, core::ProfSection::Labels);
        labels3d_.Draw(camera_.ViewProj());
    }

    dbg_.frame.drawnBatches = renderer_.DrawnBatches();
    dbg_.frame.totalBatches = renderer_.BatchCount();
    dbg_.frame.drawnTris    = renderer_.DrawnTriangles();
    dbg_.frame.totalTris    = renderer_.TriangleCount();
    dbg_.frame.hiddenProps  = renderer_.HiddenProps();
    dbg_.frame.textureBinds = renderer_.TextureBinds();
    dbg_.frame.animPlaying  = lastPlayed_ && lastPlayed_->is_playing();
    dbg_.frame.animTime     = lastPlayed_
                                  ? static_cast<float>(lastPlayed_->get_current_animation_position())
                                  : 0.0f;
    dbg_.frame.totalTargets = static_cast<int>(players_.size());
    dbg_.frame.animsPlayed  = tree_->animation_stats().plays;
    dbg_.frame.missingAnims = tree_->animation_stats().missing;
}

void GameApp::DrawQuad(const game::UiDrawItem& item) {
    if (item.texture >= static_cast<int>(uiTextures_.size())) return;
    // texture -1 is a flat ColorRect fill: the 1x1 white, tinted.
    const render::SpriteTexture& tex =
        item.texture < 0 ? uiWhite_ : uiTextures_[static_cast<std::size_t>(item.texture)];
    sprites_.Draw(tex, item.x, item.y, item.w, item.h, item.rgba);
}

void GameApp::DrawTextRun(const game::UiTextRun& run) {
    if (run.control < 0) return;
    const std::size_t ci = static_cast<std::size_t>(run.control);
    if (ci >= faceOfControl_.size()) faceOfControl_.resize(ci + 1, -2);
    if (faceOfControl_[ci] == -2) faceOfControl_[ci] = fonts_.Find(run.fontKey);
    const int face = faceOfControl_[ci];
    if (face < 0) return;
    // Laid out in the control's UNSCALED box at the font's own size, then
    // scaled with the control about its top left (Control.scale, pivot 0).
    glyphs_.clear();
    // Godot trims to int(size.width), the unscaled box (label.cpp _shape).
    const std::string trimmed =
        run.trimChars ? fonts_.TrimToWidth(face, run.text, std::floor(run.w / run.sx)) : std::string();
    fonts_.Layout(face, run.trimChars ? trimmed : run.text, 0.0f, 0.0f, run.w / run.sx, run.h / run.sy, run.alignH,
                  run.alignV, glyphs_, run.lineSpacing);
    for (const assets::TextQuad& g : glyphs_) {
        sprites_.DrawRegion(FontPage(g.page), run.x + g.x * run.sx, run.y + g.y * run.sy, g.w * run.sx,
                            g.h * run.sy, g.u0, g.v0, g.u1, g.v1, run.rgba);
    }
}

// One screen's fills and text, in tree order (game::UiDrawSequence).
void GameApp::DrawScreen(const game::UiScreen& screen) {
    screen.BuildDrawList(drawItems_);
    if (textReady_) {
        screen.BuildTextList(textRuns_);
    } else {
        textRuns_.clear();
    }
    game::UiDrawSequence(drawItems_, textRuns_, drawSteps_);
    for (const game::UiDrawStep& step : drawSteps_) {
        if (step.text) {
            DrawTextRun(textRuns_[step.index]);
        } else {
            DrawQuad(drawItems_[step.index]);
        }
    }
}

// The port's title logo, the menu's alone (SceneFiles::logo): uploaded while
// that scene is loaded, freed with the next. Missing -> the text title.
void GameApp::LoadSceneLogo() {
    logo_.Destroy();
    if (scene_.logo.empty()) return;
    assets::RawImage img;
    if (!assets::ReadRawRgba(scene_.logo, img) || !logo_.Upload(img.rgba.data(), img.width, img.height)) {
        Log("menu: no logo at " + scene_.logo + " -- the text title draws instead");
        return;
    }
    Log(Printf("menu: logo %dx%d", img.width, img.height));
}

void GameApp::DrawOverlayItems(const OverlayDraw& draw) {
    for (const FillRect& f : draw.fills) sprites_.Draw(uiWhite_, f.x, f.y, f.w, f.h, f.rgba);
    if (logo_.Valid()) {
        for (const FillRect& r : draw.logo) sprites_.Draw(logo_, r.x, r.y, r.w, r.h, r.rgba);
    }
    for (const TextItem& item : draw.text) {
        for (const assets::TextQuad& q : item.quads) {
            sprites_.DrawRegion(FontPage(q.page), q.x, q.y, q.w, q.h, q.u0, q.v0, q.u1, q.v1, item.rgba);
        }
    }
}

// Under the scene's own controls, so the menu splash's black covers them: the
// port's title text and the ACHIEVEMENTS entry.
void GameApp::DrawUnderInterface() {
    if (!textReady_) return;
    achDraw_.Clear();
    if (scene_.name == "menu") {
        const PortBranding::LogoSize logo =
            logo_.Valid() ? PortBranding::LogoSize{logo_.Width(), logo_.Height()} : PortBranding::LogoSize{};
        PortBranding::Layout(*tree_, fonts_, logo, achDraw_);
    }
    if (achEntry_) {
        achScreen_.SetEntrySelected(achEntry_->has_focus());
        achScreen_.LayoutEntry(fonts_, achEntry_->is_visible_in_tree(), achDraw_);
    }
    DrawOverlayItems(achDraw_);
}

// OVER everything: the open ACHIEVEMENTS screen and the unlock toast.
void GameApp::DrawAchievements() {
    if (!textReady_) return;
    achDraw_.Clear();
    achScreen_.LayoutScreen(fonts_, achDraw_);
    toast_.Layout(fonts_, achDraw_);
    DrawOverlayItems(achDraw_);
}

void GameApp::DrawInterface() {
    if (sceneBroken_ || !uiReady_) return;
    core::ProfScope draw(&profiler_, core::ProfSection::Draw);
    core::ProfScope ui(&profiler_, core::ProfSection::Ui);


    // Over the composite, at native resolution, in design-space pixels:
    // `dialogue UI` sits after `post processing` in scene order. The interface
    // goes in the SAME letterboxed rect as the composite, so a pointer mapped
    // through platform::ToViewport hits what it is pointing at.
    const platform::ViewportRect rect = platform::ViewportRectFor(
        static_cast<float>(config_.windowWidth), static_cast<float>(config_.windowHeight),
        static_cast<float>(uiLayout_.DesignWidth()), static_cast<float>(uiLayout_.DesignHeight()));
    glViewport(static_cast<GLint>(rect.x), static_cast<GLint>(rect.y),
               static_cast<GLsizei>(rect.width), static_cast<GLsizei>(rect.height));
    sprites_.Begin(uiLayout_.DesignWidth(), uiLayout_.DesignHeight());

    // Tree order: the dialogue box is first in `dialogue UI`, then the open
    // screen; within each, every Control's fill and text in scene order, so a
    // ColorRect covers the text before it.
    DrawUnderInterface();
    DrawScreen(dialogueScreen_);
    DrawScreen(uiScreen_);

    DrawAchievements();
    overlay_.Draw(sprites_, fonts_, fontPages_, uiWhite_, dbg_);
    sprites_.End();

    dbg_.frame.uiQuads = sprites_.QuadsQueued();
    dbg_.frame.uiDrawCalls = sprites_.DrawCalls();
}

std::string GameApp::Census() const {
    return debug::FormatCensus(dbg_);
}

// The control that has focus in the Viewport GUI (godot/gui.cpp).
const char* GameApp::FocusedControlName() const {
    const godot::Control* f = tree_->gui_get_focus_owner();
    return f ? f->get_name().c_str() : "none";
}

}  // namespace app
