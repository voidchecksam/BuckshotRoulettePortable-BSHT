// The game as both entry points run it: the uploaded room, the godot:: scene
// tree and its AnimationPlayers, the renderer and its Label3D pass, the 2D
// interface and the dialogue box, the dealer's words, the camera rig, the
// session and the debug overlay. A feature is wired here, once.
//
// main() keeps only what differs per platform: the window or vitaGL context,
// reading input, loading the room file, the post pass, the audio device, where
// log lines go, and platform diagnostics.
#pragma once

#include "app/light_uniforms.h"
#include "app/scenario_runner.h"
#include "app/shot_hold.h"
#include "app/scene_soak.h"
#include "app/device_features.h"
#include "app/achievements_screen.h"
#include "app/scene_host.h"
#include "app/data_layout.h"

#include "assets/animation.h"
#include "assets/font.h"
#include "assets/label3d.h"
#include "assets/room.h"
#include "assets/ui_layout.h"
#include "core/profiler.h"
#include "debug/log.h"
#include "debug/overlay.h"
#include "debug/state.h"
#include "game/achievements.h"
#include "game/music_router.h"
#include "game/stream_router.h"
#include "assets/audio_streams.h"
#include "game/save_store.h"
#include "game/session.h"
#include "game/ui_screen.h"
#include "godot/control.h"
#include "godot/scene_tree.h"
#include "platform/input.h"
#include "platform/sound_play.h"
#include "render/camera.h"
#include "render/label3d_renderer.h"
#include "render/particle_renderer.h"
#include "render/scene_renderer.h"
#include "render/sprite_batch.h"
#include "scripts/bind_support.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace godot { class AnimationPlayer; }
namespace scripts {
class CameraManager;
class Dialogue;
}

namespace app {

struct AppConfig {
    // Where the extracted game data lives (app/data_layout.h): assets/extracted
    // on the desktop, the game folder on the device.
    std::string dataRoot;
    std::string buildId;
    // Where the translated scripts' saves go: "save" beside the desktop exe,
    // "ux0:data/bsht" on the device. Set per platform by CMake.
    std::string saveDir = BSHT_SAVE_DIR;
    // Where the baked rooms are. The boot scene's room is read by the entry
    // point; every other scene's by GameApp::ChangeScene.
    std::string roomRoot = BSHT_ROOM_ROOT;
    // The port's own files (the menu logo): assets/baked, or app0:data in the vpk.
    std::string portRoot = BSHT_ROOM_ROOT;
    std::string achievementsPath;   // the achievement text (app/data_layout.h)
    // The platform's buffer swap, so a scene change can show the loading
    // screen while it reads the next room. Optional.
    std::function<void()> swap;

    // The 3D target's aspect, not the window's: 720x408 and 960x544 are close
    // enough that the wrong one would look almost right.
    float sceneAspect = 720.0f / 408.0f;
    float farPlane = 2000.0f;

    bool cull = true;
    // Diagnostic: the old invented PS1 look (320x240 vertex snap, affine UV).
    // The original draws neither; see SceneRenderer::SetSnap.
    bool psxLook = false;
    bool autoplay = false;
    // --autoplay-self: autoplay aims at the player instead of the dealer, to
    // reach a death (and the scene change it triggers) without a hand on the pad.
    bool autoplaySelf = false;

    // Debug: open a different UI screen and step focus before the first frame.
    const char* uiScreenPrefix = nullptr;
    bool startWithOverlay = false;

    // Skip the menu and start at the table. Every diagnostic path (autoplay,
    // --play-at, --ui-screen, --debug-ray, a frame grab of the table) wants the
    // game, not a menu, so they all set this.
    bool skipMenu = false;

    // Diagnostic: begin at this batch (0-2) instead of the first. The original
    // reaches later batches only by winning the earlier ones; this is how a
    // run reaches an item round without playing through them. 0 is the real
    // start and changes nothing.
    int startBatch = 0;
    // The locale the first scene starts in ("EN", "FR", "ES LATAM", ...);
    // --locale on desktop. A later TranslationServer.set_locale wins.
    std::string locale = "EN";
    // --unlock ach8@3: unlocks that achievement at that many seconds (desktop
    // diagnostic, for the toast).
    std::string debugUnlock;

    // Diagnostic: replay a parity scenario (its JSON text) at a fixed 1/60 s
    // through app::ScenarioRunner, exactly as bsht_parity drives it, so the
    // frames it names in "shots" can be compared with Godot's. Skips the menu
    // and the session start: the scenario seeds the batch itself.
    std::string scenarioJson;
    // Frames a scenario shot's state is redrawn before it is read (app/shot_hold.h):
    // 0 on the desktop; the Vita build sets it, for Vita3K's late surface sync.
    int scenarioShotHoldFrames = 0;
    // The Vita: run only the engine paths proven on the device (app/device_features.h).
    // The desktop keeps every path on; either way the BSHT_NO_/BSHT_FORCE_ switches decide last.
    bool deviceDefaults = false;
    // The music bus's low-pass cutoff x this (app/music_cutoff.h): 1 on the desktop
    // (Godot's), raised on the Vita so its speaker hears the muffled table music.
    float musicCutoffScale = 1.0f;

    // The title the menu and the loading screen draw. The entry point owns the
    // literal; it is the same one the desktop window is opened with.
    std::string title;

    // Swap the streaming music track. The menu's own track plays over loading
    // and the menu; the room's starts when the round does, as the original
    // arranges it. Unset when the entry point has no audio device.
    std::function<void(const char* path, float gainL, float gainR)> playMusic;
    // The track the entry point started for the loading screen ("" when it
    // started none): the boot menu goes on with it if it plays the same file.
    std::string menuTrackPath;

    // The track the round plays, handed over rather than hardcoded: the two
    // platforms keep their audio in different places.
    std::string roomMusicPath;
    // Where the batch tracks live. MusicManager names the file;
    // game::MusicRouter joins the two.
    std::string musicDir;
    // The game folder's tracks have underscores for spaces.
    bool musicSpaceFreeNames = false;
    // The streamed track's gain, for SpeakerController's fades (the briefcase
    // ducking). playMusic starts a track; this rides its volume.
    std::function<void(float gainL, float gainR)> setMusicGain;
    // The master bus, linear (OptionsManager's volume through AudioServer).
    std::function<void(float)> setMasterGain;
    // The Vita hides the controller-rebinding button.
    bool hideDesktopOptions = false;   // the Vita: app::HideDesktopOnlyOptions
    // The Vita's hold-to-exit hint names its pad button (ExitManager).
    bool vitaPadNames = false;
    // The original's "reset" (R / pad button 3: clear the run, restart it).
    // The Vita's button 3 is Triangle, under the thumb: off there.
    bool padResetsRun = true;
    // The music bus's low-pass cutoff in Hz (0: none), as FilterController
    // pans it: muffled at the table, open in the lobby.
    std::function<void(float)> setMusicCutoff;
    // The other stream voices: every player of a file the bank does not hold
    // streams on one. `voice` is 0 .. StreamRouter::kVoices-1; the entry
    // point's wiring maps it past the music voice.
    std::function<void(int voice, const char* path, bool loop, float gainL, float gainR, float pitch, bool musicBus)>
        playStream;
    std::function<void(int voice)> stopStream;
    std::function<void(int voice, float gainL, float gainR)> setStreamGain;
    // True when the SFX bank holds `name` (it plays from memory); false streams.
    std::function<bool(const char* name)> isBanked;

    // Quitting from the menu. The entry points end their own frame loop.
    std::function<void()> quit;
    // Free memory as one log line (the Vita: vglMemFree), logged around a
    // scene change. Empty on desktop.
    std::function<std::string()> memoryReport;

    // Diagnostic: once `playAt` seconds have passed, play `playAnimation` on the
    // AnimationPlayer at `playPlayer`, and log every animation sound. <0 is
    // off. Lets a scripted run grab frames of one animation without input.
    float playAt = -1.0f;
    std::string playPlayer;
    std::string playAnimation;

    // Diagnostic: a viewport point (960x540). After each frame the collider
    // under it, as MouseRaycast would find it, is logged when it changes.
    // Negative is off.
    float debugRayX = -1.0f;
    float debugRayY = -1.0f;

    // Diagnostic: scripted input. At `at` seconds the event is parsed, and a
    // press is released on the next frame, so a run can reach an interaction
    // and grab frames without a human at the keyboard or mouse.
    struct AutoInput {
        enum class Kind { Key, Move, Click, Pad };   // Pad: keycode = joypad button index
        float at = 0.0f;
        Kind kind = Kind::Key;
        std::int64_t keycode = 0;   // Key
        float x = 0.0f;             // Move and Click, in viewport pixels
        float y = 0.0f;
    };
    std::vector<AutoInput> autoplayInput;

    // The window, for the letterbox the interface is drawn into. Godot's
    // stretch `viewport` + aspect `keep` centres 960x540 behind black bars, and
    // pointer input is mapped through the same rect.
    int windowWidth = 960;
    int windowHeight = 544;

    // A one-shot sound by bank name. The platform owns the audio device.
    // One play of a banked sound on Godot's terms; returns the voice's handle
    // (-1: none). stopVoice fades one out (Godot's stop).
    std::function<int(const platform::SoundPlay& play)> playSound;
    std::function<void(int voice)> stopVoice;
    std::function<void()> stopAllVoices;   // a scene change: every voice fades
    std::function<void(int voice, float gainL, float gainR, float pitch, float shelf)> updateVoice;

    // The pointer, as CursorManager sets it: Input.set_mouse_mode hides it
    // while the controller drives, set_custom_mouse_cursor swaps the image.
    // The Vita leaves both unset: touch draws no cursor.
    std::function<void(bool visible)> showCursor;
    std::function<void(const std::string& image, godot::Vector2 hotspot)> setCursor;

    // Progress and diagnostic lines: stdout on desktop, the report on device.
    debug::LogSink log;
};

// "T:key:NAME", "T:move:X,Y" or "T:click:X,Y", comma-separated in time order
// (NAME: up, down, left, right, enter; X,Y in 960x540 viewport pixels) -> the
// events, in order. An unparsable entry is skipped and named in `error`.
std::vector<AppConfig::AutoInput> ParseAutoplayInput(const std::string& spec, std::string& error);

// Points every data path in `config` at `layout`.
void ApplyDataLayout(AppConfig& config, const DataLayout& layout);

class GameApp {
public:
    GameApp() = default;
    GameApp(const GameApp&) = delete;
    GameApp& operator=(const GameApp&) = delete;
    ~GameApp();   // clears the print sink, which points at this

    // Needs a current GL context. Takes the loaded room, and the font atlas the
    // entry point read before the room so the loading screen could draw (the
    // atlas is read once). An atlas that failed to load arrives empty and the
    // game runs without text. Returns false, having logged why, when something
    // the game cannot run without is missing.
    bool Init(const AppConfig& config, assets::Room room, assets::FontAtlas fonts);

    // One frame of game and presentation state: the debug overlay, interface
    // navigation, the session, the dialogue box, the scene tree (animation and
    // animation audio) and the camera.
    void Update(float dt, const platform::FrameInput& in);

    // The room and its Label3D, through Camera(). Call with the 3D target bound.
    void DrawScene();

    // The 2D interface, the dialogue box and the debug overlay, in design
    // space. Call after the post chain has composited.
    void DrawInterface();

    // When set, Update leaves the camera alone (the desktop's orbit view).
    void SetCameraOverride(bool on) { cameraOverride_ = on; }
    render::Camera& Camera() { return camera_; }

    // --scenario: the `at` of a shot the frame just processed asks for (-1 for
    // none), the scenario's name, and whether it has run out.
    int PendingScenarioShot() const { return pendingShot_; }
    const std::string& ScenarioName() const { return runner_.name(); }
    bool ScenarioFinished() const { return scenarioDone_ && !shotHold_.Holding(); }
    // The scene as Godot left it after _ready; animation runs in it and the
    // renderer draws from it.
    godot::SceneTree& Tree() { return *tree_; }
    const godot::SceneTree& Tree() const { return *tree_; }
    const render::SceneRenderer& Renderer() const { return renderer_; }
    // Hardware probe (debug/hw_probe): the variant the next DrawScene uses.
    void SetProbeVariant(int variant) { probeVariant_ = variant; }
    debug::DebugState& Debug() { return dbg_; }

    // The census, formatted for a log or report.
    std::string Census() const;

    // The per-player counts are a scan of every AnimationPlayer. They are kept
    // current while the overlay shows them; a report asks for them here.
    void RefreshTargetStats();

    bool InterfaceReady() const { return uiReady_; }

    // The menu gates the round: until it is left, no session exists.
    bool InMenu() const { return scene_.name == "menu"; }
    // --menu-accept: presses ui_accept on the menu's first focus next frame.
    void ScriptMenuAccept() { menuAcceptPending_ = true; }
    // Leaves the menu and starts the round. False, having logged why, if the
    // session could not start.
    bool StartGame();
    // get_tree().change_scene_to_file, serviced between frames: the old tree
    // is destroyed (with every coroutine on it) and main.tscn is built again,
    // as Godot reloads a scene. See the definition for what carries over.
    void ChangeScene(const std::string& path);
    // Name of the focused control, or "none".
    const char* FocusedControlName() const;

private:
    bool LoadInterface();
    void RunScenarioFrame();
    FeatureVerdict DecideDeviceFeature(DeviceFeature f);
    void SyncCamera();
    void BindAnimation();
    // Attaches and binds the translated scripts, then runs their _ready.
    void BindScripts();
    // The scene's stream and music routers; called before _ready.
    void BindSceneAudio();
    // Hands the music router's change, if any, to the platform's music voice.
    void SyncMusic();
    // The other streamed players (game/stream_router.h): forget freed ones,
    // then apply what changed to the platform's stream voices.
    void SyncStreams();
    std::string StreamPath(const std::string& stream) const;
    // PORT: a boot that skips the intro starts the camera where the intro
    // would leave it, at CameraManager's "home" socket.
    // PORT: a boot that skips the intro starts the camera where the intro
    // would leave it, at CameraManager's "home" socket.
    void SnapCameraHome();
    // The dialogue box from DialogueManager's Label and backdrop nodes.
    void SyncDialogueBox();
    void HandleDebug(const platform::FrameInput& in);
    void BindInput();
    void RunPlayAt(float dt);
    void RunAutoplayInput(float dt);
    void RunAutoplay(float dt);
    void Push(std::int64_t code, const char* what);
    void ReleasePressedKeys();
    // Pushes CursorManager's decisions out to the platform when they change.
    void SyncCursor();
    // play() on the AnimationPlayer at `player`; nullptr when there is none.
    godot::AnimationPlayer* PlayOn(const std::string& player, const std::string& animation);
    void DrawQuad(const game::UiDrawItem& item);
    void DrawTextRun(const game::UiTextRun& run);
    void DrawScreen(const game::UiScreen& screen);
    void Log(const std::string& line) const;

    AppConfig config_;
    bool cameraOverride_ = false;

    // Declaration order is construction order, and several of these keep
    // pointers into earlier ones: the room outlives everything bound to it.
    assets::Room room_;

    // The players' animations. Before the tree: its AnimationPlayers point here.
    assets::AnimationLibrary animLib_;

    // Before the renderer: its props point at these nodes.
    // Owned through a pointer so a scene change can build a fresh one
    // (ChangeScene): SceneTree is neither copyable nor resettable.
    std::unique_ptr<godot::SceneTree> tree_ = std::make_unique<godot::SceneTree>();
    std::vector<godot::AnimationPlayer*> players_;   // every AnimationPlayer, for the stats
    const godot::AnimationPlayer* lastPlayed_ = nullptr;
    float playClock_ = 0.0f;
    bool playFired_ = false;
    // --play-at "P|anim;P2|anim2@2.2": later plays, each `delay` seconds after
    // the first (the muzzle flash fires 2.2 s into "player shoot dealer").
    struct ChainedPlay { std::string player, animation; float at = 0.0f; bool fired = false; };
    std::vector<ChainedPlay> playChain_;
    double inputClock_ = 0.0;
    std::size_t nextAutoInput_ = 0;
    std::vector<std::int64_t> releaseNextFrame_;
    // --autoplay drives the shooting choice through the real input path.
    int   autoplayStep_ = 0;
    float autoplayDelay_ = 0.0f;
    godot::Vector2 lastPointer_;
    bool releaseClickNextFrame_ = false;

    render::SceneRenderer renderer_;
    DynLights dynLights_;   // the four animated lights
    render::ParticleRenderer particles_;   // the smoke
    bool particlesTried_ = false;
    bool particlesReady_ = false;
    uint64_t particlesLive_ = 0;   // the systems the log last reported live
    int dynLogged_ = 0;            // animated lights burning when last logged
    uint64_t particleFreeGeneration_ = 0;
    void BindParticles();
    MovingLight movingLight_;     // the street light that sweeps the car
    EnvironmentGrade envGrade_;   // the Environment's brightness/contrast
    bool saturationReported_ = false;
    int fogLogged_ = -1;   // the fog state the log last reported (-1: none yet)
    int probeVariant_ = 0;   // debug::ProbeVariant
    render::Camera camera_;

    assets::UiLayout uiLayout_;
    std::vector<render::SpriteTexture> uiTextures_;
    render::SpriteBatch sprites_;
    render::SpriteTexture uiWhite_;
    render::SpriteTexture logo_;   // the port's title logo while the menu is the scene
    assets::FontAtlas fonts_;
    // One texture per atlas page (index = page). Base pages are uploaded with
    // the interface; a CJK group's pages only while its locale is chosen.
    std::vector<render::SpriteTexture> fontPages_;
    std::string fontGroup_;          // the group whose pages are uploaded
    std::string fontLocale_;         // the locale that choice was made for
    void SyncFontGroup();
    const render::SpriteTexture& FontPage(int page) const;
    bool uiReady_ = false;
    // A scene change failed after the old tree was gone: see AbandonScene.
    bool sceneBroken_ = false;
    void AbandonScene();
    bool textReady_ = false;

    bool menuAcceptPending_ = false;
    // BSHT_MENU_ACCEPT (a file on the Vita): START pressed by itself 5 s into
    // the menu, so Vita3K can run a player's boot (its window takes no scripted
    // keys).
    float menuAcceptIn_ = -1.0f;
    // The ACHIEVEMENTS screen, its menu entry and the unlock toast.
    AchievementsScreen achScreen_{achievements_};
    AchievementToast toast_;
    OverlayDraw achDraw_;
    godot::Button* achEntry_ = nullptr;
    float debugClock_ = 0.0f;
    void AddAchievementsEntry();
    bool HandleAchievementsInput(const platform::FrameInput& in);
    void DrawAchievements();
    void DrawUnderInterface();
    void DrawOverlayItems(const OverlayDraw& draw);
    void LoadSceneLogo();

    game::MusicRouter musicRouter_;

    assets::AudioStreams audioStreams_;   // every stream's length and loop flag

    game::StreamRouter streamRouter_;

    uint64_t streamFreeGeneration_ = 0;
    bool musicDucked_ = false;
    game::UiScreen uiScreen_{uiLayout_};
    game::UiScreen dialogueScreen_{uiLayout_};
    std::string lastDebugRayHit_;
    std::string lastInteractionLog_;
    std::string lastCursorImage_;
    int lastMouseMode_ = -1;
    int  dialogueText_ = -1;
    int  dialogueBackdrop_ = -1;
    bool dialogueReady_ = false;

    // Reused every frame so the interface never allocates in the loop.
    std::vector<game::UiDrawItem> drawItems_;
    std::vector<game::UiTextRun>  textRuns_;
    std::vector<game::UiDrawStep> drawSteps_;
    std::vector<assets::TextQuad> glyphs_;
    // Font face per UI control: -2 not looked up yet, -1 none. A string
    // search per label per frame, done once instead.
    mutable std::vector<int> faceOfControl_;

    assets::Labels3D labelData_;
    render::Label3DRenderer labels3d_;
    bool labels3dReady_ = false;

    // The translated scripts on the tree's nodes. Owned by those nodes.
    scripts::BindResult scripts_;
    // Persistence and local achievements. Created in Init from
    // config_.saveDir; every Achievement script in a bound tree is pointed at
    // achievements_.
    std::unique_ptr<game::SaveStore> saves_;
    // A scenario that asks for "pills": a throwaway store holding only the
    // Double or Nothing unlock, never the player's saves.
    std::unique_ptr<game::SaveStore> scenarioSaves_;
    // The scene resident now: its room, runtime tree, animation, interface and
    // Label3D files. ChangeScene swaps all of them.
    SceneFiles scene_;
    // The interface half that belongs to a scene: its layout, textures and
    // open screens. LoadInterface does the once-only half, then this.
    bool LoadSceneInterface();
    // Reads and uploads another scene's room behind the loading screen.
    bool LoadSceneRoom(const SceneFiles& files);
    game::Achievements achievements_;
    void BindPersistence();
    void LoadInputMapOnce();
    void SyncMasterVolume();
    bool inputMapLoaded_ = false;
    float masterDb_ = 0.0f;
    uint32_t muteMask_ = 0;   // AudioServer::mute_mask, logged on a change
    scripts::Dialogue* dialogueScript_ = nullptr;
    scripts::CameraManager* cameraManager_ = nullptr;

    debug::DebugState dbg_;
    std::vector<std::string> inaudible_;   // streams already logged as played at zero gain
    // The per-system frame profiler. Its line is logged every kProfilerWindow
    // frames (desktop console and the Vita report alike).
    static constexpr int kProfilerWindow = 600;
    core::Profiler profiler_;
    void FoldProfilerFrame();
    debug::Overlay overlay_;
    std::vector<std::string> screenPrefixes_;

    // Last: the session's sequences point into everything above.
    game::Session session_;
    ScenarioRunner runner_;
    bool scenarioActive_ = false;
    bool scenarioDone_ = false;
    int pendingShot_ = -1;
    std::optional<SceneSoak> soak_;   // BSHT_SCENE_SOAK (app/scene_soak.h)
    ShotHold shotHold_{0};   // set from config_.scenarioShotHoldFrames in Init
    bool shotHandshake_ = false;   // BSHT_SHOT_HANDSHAKE: hold until shot_<N>.done
};

}  // namespace app
