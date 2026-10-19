// ShotgunShooting's shot, on a small tree wired the way main.tscn wires it: the
// shotgun's AnimationPlayer, the shell sequence, the round's health, and the
// speakers the shot plays. The camera is given a speaker but no sockets
// (BeginLerp only touches `cam` inside the socket loop), so the shot can be
// tested without the 23 CameraSocket resources.
#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/CameraShaker.h"
#include "scripts/DeathManager.h"
#include "scripts/FilterController.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/CursorManager.h"
#include "scripts/HandcuffManager.h"
#include "scripts/HealthCounter.h"
#include "godot/particles.h"
#include "scripts/ItemManager.h"
#include "scripts/MusicManager.h"
#include "scripts/RoundManager.h"
#include "scripts/ObjectShaker.h"
#include "scripts/PickupIndicator.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellLoader.h"
#include "scripts/ShellSpawner.h"
#include "scripts/SmokeController.h"
#include "scripts/TimeScaleManager.h"
#include "scripts/ShotgunShooting.h"
#include "scripts/resources.h"

#include "godot/material.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

template <class T>
T* Attach(godot::Node* node) {
    auto script = std::make_unique<T>();
    T* raw = script.get();
    node->set_script(std::move(script));
    return raw;
}

struct ShootingTree : ::testing::Test {
    godot::SceneTree t;
    scripts::ShotgunShooting* shooting = nullptr;
    scripts::RoundManager* roundManager = nullptr;
    scripts::ShellSpawner* shellSpawner = nullptr;
    scripts::HealthCounter* healthCounter = nullptr;
    scripts::CameraManager* camera = nullptr;
    scripts::CursorManager* cursor = nullptr;
    godot::AudioStreamPlayer* speakerLive = nullptr;
    godot::AudioStreamPlayer* speakerBlank = nullptr;
    scripts::PlayerData playerData;
    std::vector<scripts::RoundClass> rounds{2};
    scripts::MusicManager* music = nullptr;
    scripts::CameraShaker* shaker = nullptr;
    scripts::ShellEjectManager* ejector = nullptr;
    scripts::ObjectShaker* gunShaker = nullptr;
    scripts::DealerIntelligence* dealer = nullptr;
    scripts::DeathManager* death = nullptr;
    godot::VisualInstance3D* meshNormal = nullptr;
    godot::VisualInstance3D* meshCrushed = nullptr;
    scripts::PickupIndicator* indicator = nullptr;
    scripts::ItemManager* itemManager = nullptr;
    godot::StaticBody3D* collider = nullptr;
    godot::Node3D* gun = nullptr;
    scripts::SmokeController* smoke = nullptr;
    scripts::TimeScaleManager* timescale = nullptr;
    godot::AudioStreamPlayer* speakerSplatter = nullptr;
    godot::AnimationPlayer* animSplatter = nullptr;

    void SetUp() override {
        godot::seed(20260920);
        godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);

        auto* anim = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("anim_shotgun main parent"), root));
        auto* flash = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_muzzle flash"), root));
        auto* flashModel = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_muzzle flash model"), root));

        godot::Node* managers = t.add_node(std::make_unique<godot::Node>("standalone managers"), root);

        auto* camNode = t.add_node(std::make_unique<godot::Node>("camera manager"), managers);
        camera = Attach<scripts::CameraManager>(camNode);
        camera->cam = static_cast<godot::Camera3D*>(
            t.add_node(std::make_unique<godot::Camera3D>("Camera"), root));
        camera->speaker_pan = static_cast<godot::AudioStreamPlayer*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_pan"), root));

        cursor = Attach<scripts::CursorManager>(
            t.add_node(std::make_unique<godot::Node>("cursor manager"), managers));

        roundManager = Attach<scripts::RoundManager>(
            t.add_node(std::make_unique<godot::Node>("round manager"), managers));
        shellSpawner = Attach<scripts::ShellSpawner>(
            t.add_node(std::make_unique<godot::Node>("shell spawner"), managers));
        healthCounter = Attach<scripts::HealthCounter>(
            t.add_node(std::make_unique<godot::Node>("health counter"), managers));

        speakerLive = static_cast<godot::AudioStreamPlayer*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_live"), root));
        speakerBlank = static_cast<godot::AudioStreamPlayer*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_blank"), root));

        shooting = Attach<scripts::ShotgunShooting>(
            t.add_node(std::make_unique<godot::Node>("shotgun shooting"), managers));
        shooting->animator_shotgun = anim;
        shooting->animator_muzzleFlash = flash;
        shooting->animator_muzzleFlash_model = flashModel;
        shooting->camera = camera;
        shooting->cursorManager = cursor;
        shooting->roundManager = roundManager;
        shooting->shellSpawner = shellSpawner;
        shooting->healthCounter = healthCounter;
        gun = static_cast<godot::Node3D*>(
            t.add_node(std::make_unique<godot::Node3D>("shotgun parent new"), root));
        collider = static_cast<godot::StaticBody3D*>(
            t.add_node(std::make_unique<godot::StaticBody3D>("StaticBody3D"), gun));
        gunShaker = Attach<scripts::ObjectShaker>(
            t.add_node(std::make_unique<godot::Node>("obj shaker branch"), gun));
        gunShaker->obj = gun;
        itemManager = Attach<scripts::ItemManager>(
            t.add_node(std::make_unique<godot::Node>("item manager"), managers));
        indicator = Attach<scripts::PickupIndicator>(
            t.add_node(std::make_unique<godot::Node>("pickup indicator"), gun));
        indicator->obj = gun;
        indicator->itemManager = itemManager;
        indicator->activeAxis = "y";
        shooting->shotgunshaker = gunShaker;
        shooting->shotgunIndicator = indicator;
        shooting->collider_shotgun = collider;
        meshNormal = static_cast<godot::VisualInstance3D*>(
            t.add_node(std::make_unique<godot::VisualInstance3D>("dealer mesh"), root));
        meshCrushed = static_cast<godot::VisualInstance3D*>(
            t.add_node(std::make_unique<godot::VisualInstance3D>("dealer mesh crushed"), root));
        dealer = Attach<scripts::DealerIntelligence>(
            t.add_node(std::make_unique<godot::Node>("dealer intelligence"), managers));
        dealer->dealermesh_normal = meshNormal;
        dealer->dealermesh_crushed = meshCrushed;

        death = Attach<scripts::DeathManager>(
            t.add_node(std::make_unique<godot::Node>("death manager"), managers));
        death->shotgunShooting = shooting;
        death->dealerAI = dealer;
        death->animator_shotgun = anim;
        death->animator_dealer = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_dealer"), root));
        death->animator_dealerHands = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_dealer hands"), root));
        death->speaker_crash = static_cast<godot::AudioStreamPlayer3D*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer3D>("speaker_crash"), root));
        death->healthCounter = healthCounter;
        death->shellLoader = Attach<scripts::ShellLoader>(
            t.add_node(std::make_unique<godot::Node>("shell loader"), managers));
        death->shellLoader->shotgunHand_L = static_cast<godot::Node3D*>(
            t.add_node(std::make_unique<godot::Node3D>("hand L"), root));
        death->shellLoader->shotgunHand_R = static_cast<godot::Node3D*>(
            t.add_node(std::make_unique<godot::Node3D>("hand R"), root));
        shooting->death = death;
        shooting->speaker_live = speakerLive;
        shooting->speaker_blank = speakerBlank;

        ejector = Attach<scripts::ShellEjectManager>(
            t.add_node(std::make_unique<godot::Node>("shell eject player"), managers));
        ejector->animator_fader = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_fader"), root));
        ejector->mat_live = godot::MaterialLibrary::get("res://test/eject live.tres", godot::SurfaceMaterial{});
        ejector->mat_blank = godot::MaterialLibrary::get("res://test/eject blank.tres", godot::SurfaceMaterial{});
        ejector->animator = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_eject"), root));
        ejector->mesh = static_cast<godot::MeshInstance3D*>(
            t.add_node(std::make_unique<godot::MeshInstance3D>("shell mesh"), root));
        ejector->shellSpawner = shellSpawner;
        shooting->ejectionManager_player = ejector;

        shaker = Attach<scripts::CameraShaker>(
            t.add_node(std::make_unique<godot::Node>("camera shaker"), managers));
        shaker->camera_to_shake = camera->cam;
        shaker->intensity = 0.1;
        shaker->duration = 0.3;
        smoke = Attach<scripts::SmokeController>(
            t.add_node(std::make_unique<godot::Node>("smoke controller"), managers));
        smoke->particle_barrel = static_cast<godot::GPUParticles3D*>(
            t.add_node(std::make_unique<godot::GPUParticles3D>("particle_smoke muzzle"), gun));
        smoke->particle_chamber = static_cast<godot::GPUParticles3D*>(
            t.add_node(std::make_unique<godot::GPUParticles3D>("particle_smoke chamber"), gun));
        timescale = Attach<scripts::TimeScaleManager>(
            t.add_node(std::make_unique<godot::Node>("timescale manager"), managers));
        timescale->pitchShiftDuration = 0.5;
        timescale->timeScaleLerpDuration = 1.0;
        speakerSplatter = static_cast<godot::AudioStreamPlayer*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_splatter"), root));
        animSplatter = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("anim_splatter"), root));
        shooting->cameraShaker = shaker;
        shooting->smoke = smoke;
        ejector->ai = dealer;      // the ejection pops the dealer's known-shell array
        ejector->smoke = smoke;    // and puffs the chamber on a live shell
        death->ejectManager_dealer = ejector;   // the dealer's side reuses it here
        shooting->timescale = timescale;
        shooting->speaker_splatter = speakerSplatter;
        shooting->anim_splatter = animSplatter;

        music = Attach<scripts::MusicManager>(
            t.add_node(std::make_unique<godot::Node>("music manager"), managers));
        roundManager->playerData = &playerData;
        roundManager->shellSpawner = shellSpawner;   // FinalizeShooting reads it
        shellSpawner->roundManager = roundManager;   // EjectShell reads back through it
        roundManager->musicManager = music;
        // StartRound awaits handcuffs.RemoveAllCuffsRoutine() immediately, and
        // Spawn runs a body straight to its first co_await.
        roundManager->handcuffs = Attach<scripts::HandcuffManager>(
            t.add_node(std::make_unique<godot::Node>("handcuff manager"), managers));
        for (auto& r : rounds) roundManager->roundArray.push_back(&r);
        roundManager->currentRound = 0;
        roundManager->currentShotgunDamage = 1;
        roundManager->health_player = 4;
        roundManager->health_opponent = 4;

        // The revival's own nodes. Wired last, where every node exists.
        death->ejectManager_player = ejector;
        death->viewblocker = static_cast<godot::ColorRect*>(
            t.add_node(std::make_unique<godot::ColorRect>("viewblocker"), root));
        death->defibParent = static_cast<godot::Node3D*>(
            t.add_node(std::make_unique<godot::Node3D>("defib parent"), root));
        death->animator_playerDefib = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_defib"), root));
        death->animator_pp = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_pp"), root));
        death->speaker_playerDefib = static_cast<godot::AudioStreamPlayer*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_defib"), root));
        death->speaker_heartbeat = static_cast<godot::AudioStreamPlayer*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_heartbeat"), root));
        death->cameraShaker = shaker;
        death->filter = Attach<scripts::FilterController>(
            t.add_node(std::make_unique<godot::Node>("filter controller"), managers));
        death->filter->panDuration = 1.0;
        death->rm = roundManager;
        // The dealer's own wiring: EndTurn now hands him the turn for real.
        dealer->roundManager = roundManager;
        dealer->shellSpawner = shellSpawner;
        dealer->shellLoader = death->shellLoader;
        dealer->animator_shotgun = anim;
        dealer->camera = camera;
        dealer->ejectManager = ejector;
        dealer->shellEject_dealer = ejector;
        dealer->death = death;
        dealer->cameraShaker = shaker;
        dealer->smoke = smoke;
        dealer->shotgunShooting = shooting;
        // DealerChoice scans the table for items (gd L119-L152): an empty one.
        itemManager->itemSpawnParent = static_cast<godot::Node3D*>(
            t.add_node(std::make_unique<godot::Node3D>("item spawn parent"), root));
        dealer->itemManager = itemManager;
        // DeathManager._ready() hides the defib rig. ShootingTree does not call
        // ready_scripts (it would fire _ready on every heavy manager), so the
        // one line of _ready that matters here is mirrored.
        death->defibParent->set_visible(false);
        death->healthCounter->roundManager = roundManager;
        death->healthCounter->camera = camera;
    }

    void Advance(double seconds) {
        const int frames = static_cast<int>(seconds * 60.0 + 0.5);
        for (int i = 0; i < frames; ++i) t.process_frame(1.0 / 60.0);
    }
};

// gd: L118-L120. The shot lands after .25 + .5 + 2 s of waits.
TEST_F(ShootingTree, LiveShotAtDealerSubtractsDamage) {
    shellSpawner->sequenceArray = {"live"};
    roundManager->health_opponent = 4;

    t.spawn(shooting->Shoot("dealer"));
    Advance(3.0);

    EXPECT_EQ(roundManager->health_opponent, 3);
    EXPECT_EQ(shooting->whoshot, "dealer");
}

// gd: L138-L140. A blank at yourself is the one branch that keeps the turn.
TEST_F(ShootingTree, BlankAtSelfLetsThePlayerGoAgain) {
    shellSpawner->sequenceArray = {"blank"};

    t.spawn(shooting->Shoot("self"));
    Advance(3.0);

    EXPECT_TRUE(shooting->playerCanGoAgain);
    EXPECT_EQ(shooting->whoshot, "player");
    EXPECT_EQ(roundManager->health_player, 4);   // a blank costs nothing
}

// gd: L119, L131. Both subtractions clamp at 0: health never goes negative.
TEST_F(ShootingTree, DamageClampsAtZero) {
    shellSpawner->sequenceArray = {"live"};
    roundManager->health_opponent = 1;
    roundManager->currentShotgunDamage = 3;

    t.spawn(shooting->Shoot("dealer"));
    Advance(3.0);

    EXPECT_EQ(roundManager->health_opponent, 0);
}

// gd: L126. The last shell in the chamber sets the original's whatTheFuck flag,
// which FinalizeShooting later spends 2 s on.
TEST_F(ShootingTree, LiveAtSelfOnTheLastShellSetsWhatTheFuck) {
    shellSpawner->sequenceArray = {"live"};
    dealer->sequenceArray_knownShell = {true};   // the ejection pops both

    t.spawn(shooting->Shoot("self"));
    Advance(3.0);

    EXPECT_TRUE(shooting->whatTheFuck);
    EXPECT_TRUE(healthCounter->playerShotSelf);
    EXPECT_TRUE(healthCounter->checkingPlayer);
    EXPECT_EQ(roundManager->health_player, 3);
}

// gd: L62-L70. The slow-motion is gated on all three: a LIVE shell, aimed at
// the dealer, that takes him to exactly 0.
TEST_F(ShootingTree, SlowMotionOnlyOnTheKillingBlow) {
    shellSpawner->sequenceArray = {"live"};
    roundManager->health_opponent = 4;       // survives, so no slow motion

    shooting->MainSlowDownRoutine("dealer", false);
    EXPECT_FALSE(timescale->moving);

    roundManager->health_opponent = 1;       // this shot kills him
    shooting->MainSlowDownRoutine("dealer", false);
    EXPECT_TRUE(timescale->moving);
    EXPECT_DOUBLE_EQ(timescale->from, .2);
    EXPECT_DOUBLE_EQ(timescale->to, 1.0);
}

// gd: L171-L192. The first observable beat of FinalizeShooting's tail: after
// .6 s it reverts the pickup indicator and re-enables the shotgun's collider,
// which is what lets you pick the gun up again. The wait stack after it
// (waitForRevival, addDelay, delaying, whatTheFuck) only becomes observable
// when EndTurn lands, so its timing is pinned in the EndTurn task.
TEST_F(ShootingTree, FinalizeRearmsTheShotgunColliderAfterSixTenths) {
    shellSpawner->sequenceArray = {"blank"};
    roundManager->health_opponent = 3;
    shooting->ShotgunCollider(false);
    ASSERT_EQ(collider->collision_layer, 0u);

    // playerCanGoAgain: EndTurn then re-enters the player's turn, which
    // suspends on its own .6 s timer. With false it would hand off to the
    // dealer, whose turn needs the ceremony, which is not what this test is
    // about.
    t.spawn(shooting->FinalizeShooting(true, false, false, false));
    Advance(0.5);
    EXPECT_EQ(collider->collision_layer, 0u);     // still inside the .6 s wait

    Advance(0.2);
    EXPECT_EQ(collider->collision_layer, 1u);     // ShotgunCollider(true) has run
    EXPECT_FALSE(shooting->shotgunshaker->shaking);
}

// gd ShellEjectManager L19-L32: the ejection is what consumes the chamber,
// sequenceArray and the dealer's sequenceArray_knownShell together. The .gd
// has the same remove_at(0) commented out in Shoot and FinalizeShooting.
TEST_F(ShootingTree, EjectingAShellConsumesItFromBothArrays) {
    shellSpawner->sequenceArray = {"live", "blank", "live"};
    dealer->sequenceArray_knownShell = {true, false, false};

    ejector->EjectShell();

    EXPECT_EQ(shellSpawner->sequenceArray.size(), 2u);
    EXPECT_EQ(shellSpawner->sequenceArray[0], "blank");
    EXPECT_EQ(dealer->sequenceArray_knownShell.size(), 2u);
    EXPECT_FALSE(dealer->sequenceArray_knownShell[0]);
    EXPECT_EQ(playerData.stat_shellsEjected, 1);
    EXPECT_FALSE(ejector->hasFaded);
}

// gd RoundManager L259-L290. EndTurn picks one of three branches. Two of them
// are unit-testable here; the third (an empty chamber chaining into the next
// round) spawns StartRound, whose prologue walks the handcuffs, the dialogue,
// the camera and the shell loader, i.e. most of the scene. That branch is
// covered by the batch1_playthrough parity scenario instead of by a fixture
// that would have to fake the ceremony.
// gd: L288-L290. `ignoring` suppresses the chain entirely.
TEST_F(ShootingTree, IgnoringSuppressesTheNextRound) {
    shellSpawner->sequenceArray.clear();
    roundManager->ignoring = true;
    const int before = roundManager->currentRound;

    t.spawn(roundManager->EndTurn(false));
    Advance(0.05);

    EXPECT_EQ(roundManager->currentRound, before);
}

// gd: L271-L273. With a shell left and playerCanGoAgain, it re-enters the
// player's turn rather than chaining a round. Advanced only to .3 s so
// BeginPlayerTurn stays suspended on its own .6 s timer.
TEST_F(ShootingTree, AShellLeftAndGoAgainReEntersThePlayerTurn) {
    shellSpawner->sequenceArray = {"blank", "live"};
    const int before = roundManager->currentRound;

    t.spawn(roundManager->EndTurn(true));
    Advance(0.3);

    EXPECT_EQ(roundManager->currentRound, before);   // did not start a new round
}

// gd DeathManager L71-L110. Everything up to the first co_await happens in one
// go: the dealer is flung away, his mesh is swapped for the crushed one, the
// hands are hidden and he is no longer at the table.
TEST_F(ShootingTree, KillingTheDealerFlingsHimAwayAndSwapsTheMesh) {
    shellSpawner->sequenceArray = {"live"};
    roundManager->health_opponent = 2;
    roundManager->dealerAtTable = true;
    ASSERT_FALSE(dealer->swapped);

    t.spawn(death->Kill("dealer", false, false));

    EXPECT_TRUE(dealer->swapped);
    EXPECT_FALSE(roundManager->dealerAtTable);
    EXPECT_FALSE(death->shellLoader->shotgunHand_L->is_visible());
    EXPECT_FALSE(death->shellLoader->shotgunHand_R->is_visible());
}

// gd: L76-L81. Only a dealer who shot HIMSELF returns the shotgun, and that is
// the branch that later ends his turn.
TEST_F(ShootingTree, ADealerWhoShotHimselfReturnsTheShotgun) {
    shellSpawner->sequenceArray = {"live"};
    // The scene keeps the dealer's known-shell array in step with the chamber,
    // and DeathEjection pops both.
    dealer->sequenceArray_knownShell = {true};
    roundManager->health_opponent = 2;
    dealer->dealerHoldingShotgun = true;

    t.spawn(death->Kill("dealer", false, /*returningShotgun=*/true));

    EXPECT_FALSE(dealer->dealerHoldingShotgun);
}

// gd DeathManager L33-L69. The revival: the view blacks out .08 s in, and
// 2 + .4 + .85 s later the defib rig is out and the blackout is lifted.
//
// The tail is not asserted here. After "remove defib device" the .gd awaits
// healthCounter.UpdateDisplayRoutine, which drives the counter's whole display,
// the same ceremony wall as StartRound. Hiding defibParent at the very end is
// covered by the shoot_self_live_b1r1 parity scenario.
TEST_F(ShootingTree, TheRevivalBlacksTheViewOutThenBringsTheDefibOut) {
    shellSpawner->sequenceArray = {"live"};
    dealer->sequenceArray_knownShell = {true};
    roundManager->health_player = 3;

    t.spawn(death->Kill("player", false, /*returningShotgun=*/true));
    Advance(0.2);
    EXPECT_TRUE(death->viewblocker->is_visible());

    // .08 + .4 + .85, just past the heartbeat: the .gd's create_timer(2) is
    // awaited without .timeout and waits nothing (parity_shoot_self_b1r1).
    Advance(1.4);
    EXPECT_FALSE(death->viewblocker->is_visible());
    EXPECT_TRUE(death->defibParent->is_visible());
}

// gd: L46. A player left on 1 health sets the original's shitIsFuckedUp, which
// FinalizeShooting then spends a further 3 s on.
TEST_F(ShootingTree, OneHealthLeftSetsShitIsFuckedUp) {
    shellSpawner->sequenceArray = {"live"};
    dealer->sequenceArray_knownShell = {true};
    roundManager->health_player = 1;

    t.spawn(death->Kill("player", false, true));
    Advance(2.2);

    EXPECT_TRUE(death->shitIsFuckedUp);
}

// gd: L52-L54. At 0 health there is no revival at all: it hands off to
// OutOfHealth and returns.
TEST_F(ShootingTree, NoRevivalWhenThePlayerIsOutOfHealth) {
    shellSpawner->sequenceArray = {"live"};
    dealer->sequenceArray_knownShell = {true};
    roundManager->health_player = 0;

    t.spawn(death->Kill("player", false, true));
    Advance(3.0);

    EXPECT_TRUE(death->viewblocker->is_visible());    // still blacked out
    EXPECT_FALSE(death->defibParent->is_visible());   // the defib never came out
}

// gd: L421-L431. Outside endless the dealer's coin flip is randi_range(0, 1),
// a true 50/50, and that is correct. The weighting and FigureOutShell
// deduction are both gated on roundManager.endless, i.e. Double or Nothing.
// This test asserts the distribution, not that the dealer is smart; if it
// fails because the dealer was "fixed", the fix is the bug.
TEST_F(ShootingTree, TheBaseGameCoinFlipIsGenuinelyRandom) {
    roundManager->endless = false;
    int zero = 0;
    int one = 0;
    for (int i = 0; i < 400; ++i) {
        const int r = dealer->CoinFlip();
        ASSERT_TRUE(r == 0 || r == 1) << "CoinFlip returned " << r;
        if (r == 0) ++zero; else ++one;
    }
    EXPECT_GT(zero, 120);
    EXPECT_GT(one, 120);
}

// gd: L106-L112. One shell left means the dealer KNOWS it, and aims
// accordingly: a live one at the player, a blank at himself.
TEST_F(ShootingTree, TheLastShellIsKnownAndAimed) {
    shellSpawner->sequenceArray = {"live"};
    dealer->dealerHoldingShotgun = true;   // skip the grab
    roundManager->waitingForDealerReturn = false;

    t.spawn(dealer->DealerChoice());
    Advance(0.05);

    EXPECT_TRUE(dealer->dealerKnowsShell);
    EXPECT_EQ(dealer->knownShell, "live");
    EXPECT_EQ(dealer->dealerTarget, "player");
}

// gd: L305-L318. At 0 health the dealer's turn ends at OutOfHealth instead of
// chaining.
TEST_F(ShootingTree, TheDealerTurnStopsWhenSomeoneIsOutOfHealth) {
    shellSpawner->sequenceArray = {"live"};
    roundManager->health_opponent = 0;
    dealer->dealerHoldingShotgun = true;

    dealer->EndDealerTurn(true);
    Advance(0.05);

    // EndDealerTurn returns at OutOfHealth, so EndTurnMain never runs: the gun
    // is never put down and the camera never goes home.
    EXPECT_TRUE(dealer->dealerHoldingShotgun);
}

// EjectShell is called from an animation METHOD track, never from script flow
// (animation_methods.json lists it 4 times). The port routes METHOD keys
// through Node::call, which forwards to Script::call, which returns false by
// default: without a dispatcher every EjectShell is silently dropped, the
// chamber never empties and the same shell fires forever. Calling EjectShell()
// directly would pass while the game is broken; this goes through the
// dispatcher.
TEST_F(ShootingTree, TheAnimationsMethodTrackCanReachEjectShell) {
    shellSpawner->sequenceArray = {"live", "blank"};
    dealer->sequenceArray_knownShell = {true, false};

    const bool dispatched = ejector->get_node_owner()->call("EjectShell", {});

    EXPECT_TRUE(dispatched) << "an unhandled METHOD key is dropped silently";
    EXPECT_EQ(shellSpawner->sequenceArray.size(), 1u) << "the shell must leave the chamber";
    EXPECT_EQ(shellSpawner->sequenceArray[0], "blank");
}

// CameraShaker and TimeScaleManager need NOTIFICATION_READY and a running
// _process, which means ready_scripts(), and that fires _ready on every script
// in the tree. They get their own minimal tree rather than wiring the scene
// dependencies of RoundManager, ShellSpawner and HealthCounter, whose _ready
// would run too.
struct ShakerTree : ::testing::Test {
    godot::SceneTree t;
    godot::Camera3D* cam = nullptr;
    scripts::CameraShaker* shaker = nullptr;
    scripts::ShellEjectManager* ejector = nullptr;
    scripts::ObjectShaker* gunShaker = nullptr;
    scripts::DealerIntelligence* dealer = nullptr;
    scripts::DeathManager* death = nullptr;
    godot::VisualInstance3D* meshNormal = nullptr;
    godot::VisualInstance3D* meshCrushed = nullptr;
    scripts::PickupIndicator* indicator = nullptr;
    scripts::ItemManager* itemManager = nullptr;
    godot::StaticBody3D* collider = nullptr;
    godot::Node3D* gun = nullptr;
    scripts::TimeScaleManager* timescale = nullptr;
    scripts::FilterController* filter = nullptr;

    void SetUp() override {
        godot::seed(20260920);
        godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
        cam = static_cast<godot::Camera3D*>(
            t.add_node(std::make_unique<godot::Camera3D>("Camera"), root));
        cam->h_offset = 0.0f;
        cam->v_offset = 0.0f;

        shaker = Attach<scripts::CameraShaker>(
            t.add_node(std::make_unique<godot::Node>("camera shaker"), root));
        shaker->camera_to_shake = cam;
        shaker->intensity = 0.1;
        shaker->duration = 0.3;

        timescale = Attach<scripts::TimeScaleManager>(
            t.add_node(std::make_unique<godot::Node>("timescale manager"), root));
        filter = Attach<scripts::FilterController>(
            t.add_node(std::make_unique<godot::Node>("filter controller"), root));
        filter->panDuration = 1.0;
        filter->lowPassDefaultValue = 20000.0;
        filter->lowPassMaxValue = 400.0;
        timescale->pitchShiftDuration = 0.5;
        timescale->timeScaleLerpDuration = 1.0;

        t.ready_scripts();
    }

    void Advance(double seconds) {
        const int frames = static_cast<int>(seconds * 60.0 + 0.5);
        for (int i = 0; i < frames; ++i) t.process_frame(1.0 / 60.0);
    }
};

// gd CameraShaker L20-L31: while the timer runs it writes the camera's offsets;
// when it expires it puts them back exactly.
TEST_F(ShakerTree, RestoresTheOffsetsWhenItExpires) {
    shaker->Shake();
    EXPECT_DOUBLE_EQ(shaker->shake_timer, shaker->duration);

    t.process_frame(1.0 / 60.0);
    EXPECT_TRUE(cam->h_offset != 0.0f || cam->v_offset != 0.0f);

    Advance(1.0);   // well past duration = 0.3
    EXPECT_FLOAT_EQ(cam->h_offset, static_cast<float>(shaker->initial_h_offset));
    EXPECT_FLOAT_EQ(cam->v_offset, static_cast<float>(shaker->initial_v_offset));
}

// gd TimeScaleManager L14-L27: lerps Engine.time_scale from start to end over
// timeScaleLerpDuration, eased with ease(c, 0.4).
TEST_F(ShakerTree, TimeScaleLerpReachesItsEndValue) {
    timescale->BeginTimeScaleLerp(.2, 1.0);
    t.process_frame(1.0 / 60.0);
    EXPECT_LT(t.get_time_scale(), 1.0);      // slowed down first

    Advance(1.5);                            // past timeScaleLerpDuration = 1.0
    EXPECT_DOUBLE_EQ(t.get_time_scale(), 1.0);
}

// gd FilterController L25-L38: the revival pans the low-pass back from its max
// to its default, eased with ease(c, 0.4). It needs _process, so it lives here.
TEST_F(ShakerTree, PansTheLowPassBackToDefault) {
    EXPECT_DOUBLE_EQ(filter->effect_lowPass_cutoff_hz, 20000.0);   // _ready snapped it

    filter->BeginPan(filter->lowPassMaxValue, filter->lowPassDefaultValue);
    EXPECT_TRUE(filter->moving);
    t.process_frame(1.0 / 60.0);
    EXPECT_LT(filter->effect_lowPass_cutoff_hz, 20000.0);

    Advance(1.5);   // past panDuration = 1.0
    EXPECT_DOUBLE_EQ(filter->effect_lowPass_cutoff_hz, 20000.0);
}

}  // namespace
