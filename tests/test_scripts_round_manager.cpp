// The round-1 ceremony from the translated scripts, on the REAL exports:
// RoundManager.MainBatchSetup(true) -> RoundIndicator -> StartRound ->
// ShellSpawner.MainShellRoutine -> ShellLoader.LoadShells -> SetupDeskUI.
// Skips until the exports and animations.json exist.
#include "scripts/DialogueManager.h"
#include "scripts/CameraManager.h"
#include "scripts/DefibBlade.h"
#include "scripts/DefibCutter.h"
#include "scripts/HealthCounter.h"
#include "game/session.h"
#include "scripts/ItemManager.h"
#include "scripts/MusicManager.h"
#include "scripts/RoundManager.h"
#include "scripts/StatueManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/bind_support.h"
#include "scripts/resources.h"

#include "assets/animation.h"
#include "godot/animation_player.h"
#include "godot/packed_scene.h"
#include "godot/rng.h"
#include "godot/runtime_scene_loader.h"
#include "godot/translation.h"

#include "scripts/intro_skip.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

std::string Extracted(const char* file) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + file;
}

bool Exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

constexpr double kFrame = 1.0 / 60.0;

struct Ceremony {
    assets::AnimationLibrary animations;   // before the tree: players point into it
    godot::SceneTree tree;
    scripts::RoundManager* roundManager = nullptr;
    scripts::ShellSpawner* shellSpawner = nullptr;
    scripts::CameraManager* camera = nullptr;
    scripts::HealthCounter* healthCounter = nullptr;
    bool ok = false;

    Ceremony() {
        for (const char* f : {"runtime_main.json", "runtime_shell.json", "animations.json", "dialogue_en.json"}) {
            if (!Exists(Extracted(f))) return;
        }
        godot::load_translations(Extracted("dialogue_en.json").c_str());
        scripts::RegisterScripts();
        if (!godot::PackedScene::load("res://instances/shell.tscn", Extracted("runtime_shell.json").c_str())) return;
        if (godot::LoadRuntimeScene(Extracted("runtime_main.json").c_str(), tree) != godot::RuntimeSceneError::None) {
            return;
        }
        if (!animations.LoadFromFile(Extracted("animations.json").c_str())) return;
        godot::BindAnimationLibrary(tree, animations);
        const scripts::BindResult r = scripts::BindScripts(tree.get_root());
        for (const std::string& e : r.errors) ADD_FAILURE() << e;
        tree.ready_scripts();
        scripts::SkipIntro(tree);   // start at the table, as the parity scenarios do

        roundManager = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
        shellSpawner = godot::script_cast<scripts::ShellSpawner>(tree.get_node("standalone managers/shell spawner"));
        camera = godot::script_cast<scripts::CameraManager>(tree.get_node("standalone managers/camera manager"));
        healthCounter =
            godot::script_cast<scripts::HealthCounter>(tree.get_node("standalone managers/health counter"));
        if (!roundManager || !shellSpawner || !camera || !healthCounter || !roundManager->playerData) return;

        godot::seed(20260915);
        roundManager->playerData->hasSignedWaiver = true;   // as if the waiver were signed
        for (const scripts::CameraSocket* s : camera->socketArray) {
            if (s && s->socketName == "home") {
                camera->cam->set_position(s->pos);
                camera->cam->set_rotation_degrees(s->rot);
                camera->cam->set_fov(static_cast<float>(s->fov));
            }
        }
        ok = true;
    }

    // Frames until the player's turn begins, or maxFrames.
    int RunToPlayersTurn(int maxFrames, int* mostShells = nullptr) {
        int frame = 0;
        while (roundManager->port_playerTurnsBegun == 0 && frame < maxFrames) {
            tree.process_frame(kFrame);
            if (mostShells) {
                *mostShells = std::max(*mostShells, static_cast<int>(shellSpawner->spawnedShellObjectArray.size()));
            }
            ++frame;
        }
        return frame;
    }
};

}  // namespace

TEST(ScriptsRoundManager, BatchOneRoundOneCeremonyReachesThePlayersTurn) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";

    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    int mostShells = 0;
    const int frames = c.RunToPlayersTurn(60 * 40, &mostShells);

    EXPECT_EQ(c.roundManager->port_playerTurnsBegun, 1) << "the ceremony never reached SetupDeskUI";
    EXPECT_EQ(mostShells, 3);
    EXPECT_EQ(c.shellSpawner->sequenceArray.size(), 3u);
    EXPECT_EQ(std::count(c.shellSpawner->sequenceArray.begin(), c.shellSpawner->sequenceArray.end(), "live"), 1);
    EXPECT_EQ(c.roundManager->health_player, 2);
    EXPECT_EQ(c.roundManager->health_opponent, 2);
    EXPECT_EQ(c.camera->activeSocket, "home");
    EXPECT_TRUE(c.roundManager->dealerAtTable);
    // The original's pacing: the dealer's entrance, the indicator, the shells,
    // the loading line and the loading all take real time.
    EXPECT_GT(frames, 60 * 15);
    EXPECT_EQ(c.tree.animation_stats().missing, 0);
    EXPECT_TRUE(c.roundManager->deskUI_parent->is_visible());
}

TEST(ScriptsRoundManager, TheCounterBootsAndShowsTheRoundsHealth) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);

    for (godot::Node3D* panel : c.healthCounter->uiArray) EXPECT_TRUE(panel->is_visible());
    ASSERT_GE(c.healthCounter->symbolArray_player.size(), 4u);
    for (std::size_t i = 0; i < c.healthCounter->symbolArray_player.size(); ++i) {
        EXPECT_EQ(c.healthCounter->symbolArray_player[i]->is_visible(), static_cast<int>(i) <= 2) << "symbol " << i;
    }
}

TEST(ScriptsRoundManager, TheRoundIndicatorSitsAtTheRoundsIndicatorNumber) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);

    ASSERT_FALSE(c.roundManager->roundArray.empty());
    const int n = c.roundManager->roundArray[0]->indicatorNumber;
    EXPECT_EQ(n, 1);
    const godot::Vector3 at = c.roundManager->roundIndicator->get_position();
    EXPECT_NEAR(at.x, c.roundManager->roundIndicatorPositions[static_cast<std::size_t>(n)].x, 1e-5f);
    EXPECT_FALSE(c.roundManager->roundIndicatorParent->is_visible());   // shut down again
}

TEST(ScriptsRoundManager, TheLoadingLineCountsOneMoreReading) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    const int before = c.roundManager->playerData->numberOfDialogueRead;
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);
    EXPECT_EQ(c.roundManager->playerData->numberOfDialogueRead, before + 1);
}

// Killing the dealer ends the batch (OutOfHealth("dealer") reaches EndMainBatch,
// gd: L476-L522): the index advances, and the next batch's ceremony reaches the
// player's turn again.
TEST(ScriptsRoundManager, KillingTheDealerEndsTheBatchAndTheNextOneBegins) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";

    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);
    ASSERT_EQ(c.roundManager->port_playerTurnsBegun, 1);
    const int batchBefore = c.roundManager->playerData->currentBatchIndex;

    c.roundManager->health_opponent = 0;
    c.tree.spawn(c.roundManager->OutOfHealth("dealer"));
    bool winnerShown = false;
    for (int f = 0; f < 60 * 6; ++f) {
        c.tree.process_frame(kFrame);
        winnerShown = winnerShown || c.roundManager->ui_winner->is_visible();
    }
    EXPECT_EQ(c.roundManager->playerData->currentBatchIndex, batchBefore + 1);
    EXPECT_TRUE(winnerShown) << "the WINNER sign never came up";

    // Batch 2 deals items, so its ceremony waits at the briefcase for the
    // player to grab them. ItemManager.BeginItemGrabbing clears the flag
    // EndMainBatch set: that flip is "the next batch reached the intake".
    ASSERT_TRUE(c.roundManager->itemManager->newBatchHasBegun);
    int frames = 0;
    while (c.roundManager->itemManager->newBatchHasBegun && frames < 60 * 60) {
        c.tree.process_frame(kFrame);
        ++frames;
    }
    EXPECT_FALSE(c.roundManager->itemManager->newBatchHasBegun)
        << "the next batch never reached the item intake";
    EXPECT_FALSE(c.roundManager->ui_winner->is_visible());
    EXPECT_EQ(c.tree.animation_stats().missing, 0);
}

// Dying runs DeathManager.MainDeathRoutine (gd: L139-L155): mark the player as
// entering from death, save, and change scene to death.tscn, whose
// TempDeathScreen immediately reloads main.tscn, where the batch restarts.
TEST(ScriptsRoundManager, DyingMarksTheSaveAndAsksForTheDeathScene) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.roundManager->health_player = 0;
    c.tree.spawn(c.roundManager->OutOfHealth("player"));
    std::string change;
    for (int f = 0; f < 60 && change.empty(); ++f) {
        c.tree.process_frame(kFrame);
        change = c.tree.take_scene_change();
    }
    EXPECT_EQ(change, "res://scenes/death.tscn");
    EXPECT_TRUE(c.roundManager->playerData->playerEnteringFromDeath);
    EXPECT_FALSE(c.roundManager->playerData->enteringFromTrueDeath);
}

// DeathManager.gd L147-L154: dying with the defib wire cut is the true death,
// both flags set, and heaven.tscn instead of death.tscn.
TEST(ScriptsRoundManager, DyingWithTheWireCutAsksForHeaven) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.roundManager->health_player = 0;
    c.roundManager->wireIsCut_player = true;
    c.tree.spawn(c.roundManager->OutOfHealth("player"));
    std::string change;
    int frames = 0;
    for (; frames < 60 && change.empty(); ++frames) {
        c.tree.process_frame(kFrame);
        change = c.tree.take_scene_change();
    }
    EXPECT_EQ(change, "res://scenes/heaven.tscn");
    EXPECT_GE(frames, 30) << "after the 0.5 s wait";
    EXPECT_TRUE(c.roundManager->playerData->playerEnteringFromDeath);
    EXPECT_TRUE(c.roundManager->playerData->enteringFromTrueDeath);
}

// At 2 charges or fewer in the last batch the defibrillator wire is cut:
// health drops to 1 and healing stops (HealthCounter.gd:91-104). DefibCutter
// shows it on screen, so a cigarette that does not heal reads as the rule it
// is.
TEST(ScriptsDefibCutter, CuttingThePlayersWireBreaksTheDisplayAndClosesTheBlade) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    scripts::DefibCutter* cutter = c.roundManager->defibCutter;
    ASSERT_NE(cutter, nullptr);
    ASSERT_NE(cutter->blade_player, nullptr);
    c.camera->activeSocket = "home";
    c.roundManager->requestedWireCut = true;
    c.roundManager->wireToCut = "player";

    c.tree.spawn(cutter->CutWire("player"));
    for (int f = 0; f < 60 * 14; ++f) c.tree.process_frame(kFrame);

    EXPECT_TRUE(cutter->displayBroken_player);
    EXPECT_FALSE(cutter->displayBroken_dealer);
    EXPECT_FALSE(cutter->healthUI_player->is_visible());
    EXPECT_TRUE(cutter->blade_player->wire_broken->is_visible());
    EXPECT_FALSE(cutter->blade_player->wire_normal->is_visible());
    EXPECT_FALSE(c.roundManager->requestedWireCut);
    EXPECT_EQ(c.roundManager->wireToCut, "");
    EXPECT_EQ(c.camera->activeSocket, "home");
    EXPECT_EQ(c.tree.animation_stats().missing, 0);
}

TEST(ScriptsDefibCutter, TheLastBatchSetsTheCutterUpBeforeItsFirstRound) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.roundManager->playerData->currentBatchIndex = 2;
    scripts::DefibCutter* cutter = c.roundManager->defibCutter;
    ASSERT_NE(cutter, nullptr);
    ASSERT_FALSE(cutter->consoleArray.empty());

    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    bool consoleLit = false;
    for (int f = 0; f < 60 * 40 && !c.roundManager->defibCutterReady; ++f) {
        c.tree.process_frame(kFrame);
        consoleLit = consoleLit || cutter->consoleArray.back()->is_visible();
    }
    EXPECT_TRUE(c.roundManager->defibCutterReady);
    EXPECT_TRUE(consoleLit) << "InitialSetup never lit the console";
    EXPECT_EQ(c.tree.animation_stats().missing, 0);
}

// With the wire cut, a cigarette heals nothing.
// That is the original's rule (HealthCounter.gd:148), but it has to LOOK like
// a refusal: the counter's error light blips instead of the beep.
TEST(ScriptsDefibCutter, ACigaretteAfterTheCutHealsNothingAndBlipsTheError) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    scripts::DefibCutter* cutter = c.roundManager->defibCutter;
    ASSERT_NE(cutter, nullptr);
    // The batch fills roundArray, which the cigarette routine reads for the
    // round's starting health.
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);
    c.roundManager->health_player = 1;
    c.roundManager->wireIsCut_player = true;
    cutter->BreakDisplay("player");

    c.healthCounter->UpdateDisplayRoutineCigarette_Player();
    bool errorShown = false;
    for (int f = 0; f < 60 * 4; ++f) {
        c.tree.process_frame(kFrame);
        errorShown = errorShown || cutter->errorUI_player->is_visible();
    }
    EXPECT_EQ(c.roundManager->health_player, 1) << "the cut wire must refuse the heal";
    EXPECT_TRUE(errorShown) << "nothing on screen said the heal was refused";
}

// "The round 2 and 3 music change isn't there." MusicManager.LoadTrack picks
// trackArray[currentBatchIndex] (gd:22-34), and the second-loop variant when
// the player is entering from death. On the real export those are three
// different files.
TEST(ScriptsMusicManager, EachBatchLoadsItsOwnTrack) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    scripts::MusicManager* music = c.roundManager->musicManager;
    ASSERT_NE(music, nullptr);
    ASSERT_EQ(music->trackArray.size(), 3u);
    ASSERT_NE(music->speaker_music, nullptr);

    std::vector<std::string> played;
    for (int batch = 0; batch < 3; ++batch) {
        c.roundManager->playerData->currentBatchIndex = batch;
        music->LoadTrack();
        EXPECT_TRUE(music->speaker_music->is_playing()) << batch;
        played.push_back(music->speaker_music->get_stream());
    }
    EXPECT_EQ(played[0], "music main_techno techno.ogg");
    EXPECT_EQ(played[1], "music main_error.ogg");
    EXPECT_EQ(played[2], "music main_room.ogg");

    // Entering from death plays the batch's second loop, once.
    c.roundManager->playerData->currentBatchIndex = 1;
    c.roundManager->playerData->playerEnteringFromDeath = true;
    music->LoadTrack();
    EXPECT_EQ(music->speaker_music->get_stream(), "music main second loop_error.ogg");
}

// Session::Start stands in for the end of the restroom intro, where
// IntroManager.MainTrackLoad calls LoadTrack: without it nothing ever starts
// the batch's music and the menu's track just keeps going.
TEST(ScriptsMusicManager, StartingTheSessionLoadsTheBatchTrack) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    ASSERT_NE(c.roundManager->musicManager, nullptr);
    c.roundManager->musicManager->speaker_music->stop();

    game::Session session;
    ASSERT_TRUE(session.Start(&c.tree));
    EXPECT_TRUE(c.roundManager->musicManager->speaker_music->is_playing())
        << "the batch's track never started";
}

// HealthCounter.UpdateDisplayRoutine plays speaker_noise when health falls
// (gd:88-90); the node carries "health counter reduce health.ogg", which the
// bank holds. This pins that the play reaches the sink.
TEST(ScriptsHealthCounter, LosingHealthPlaysTheCounterNoise) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    std::vector<std::string> played;
    c.tree.set_audio_sink([&played](const godot::AudioPlayback& a, const godot::AudioPlayRequest&) {
        played.push_back(a.get_stream());
        return godot::AudioPlayResult{};
    });
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);
    played.clear();

    c.roundManager->health_player -= 1;
    c.tree.spawn(c.healthCounter->UpdateDisplayRoutine(false, true, false));
    for (int f = 0; f < 60 * 6; ++f) c.tree.process_frame(kFrame);

    EXPECT_NE(std::find(played.begin(), played.end(), "health counter reduce health.ogg"),
              played.end())
        << "the counter never played its health-loss sound";
}

namespace {

// v2.2's RoundManager: a round without items hides the grid brackets, so the
// pad goes straight to the shotgun (SetupDeskUI); the round indicator checks
// the statue; _ready resets the time scale.
TEST(ScriptsRoundManagerV22, ARoundWithoutItemsHidesTheGrids) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    ASSERT_FALSE(c.roundManager->deskUI_grids.empty());
    for (godot::Control* g : c.roundManager->deskUI_grids) g->set_visible(true);
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);   // batch 1 deals no items
    ASSERT_EQ(c.roundManager->port_playerTurnsBegun, 1);
    for (godot::Control* g : c.roundManager->deskUI_grids) EXPECT_FALSE(g->is_visible());
}

TEST(ScriptsRoundManagerV22, TheRoundIndicatorChecksTheStatue) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.roundManager->playerData->playername = " vellon";
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    c.RunToPlayersTurn(60 * 40);
    EXPECT_TRUE(c.roundManager->statue->statue->is_visible());
}

TEST(ScriptsRoundManagerV22, ReadyResetsTheTimeScale) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.tree.set_time_scale(0.5);
    c.roundManager->_ready();
    EXPECT_DOUBLE_EQ(c.tree.get_time_scale(), 1.0);
}

}  // namespace

// v2.2's text keys carry the count (a %s inside each line): the line is the
// two formatted halves, with no number in front of either.
TEST(ScriptsRoundManagerV22, TheShellCountPutsTheNumbersInTheText) {
    Ceremony c;
    if (!c.ok) GTEST_SKIP() << "run the exporters (runtime scenes, animations, dialogue)";
    c.tree.spawn(c.roundManager->MainBatchSetup(true));
    std::vector<std::string> shown;
    for (int f = 0; f < 60 * 40 && c.roundManager->port_playerTurnsBegun == 0; ++f) {
        c.tree.process_frame(kFrame);
        const std::string t = c.shellSpawner->dialogue->dialogueUI->get_text();
        if (shown.empty() || shown.back() != t) shown.push_back(t);
    }
    EXPECT_NE(std::find(shown.begin(), shown.end(), "1 LIVE ROUND. 2 BLANKS."), shown.end());
}
