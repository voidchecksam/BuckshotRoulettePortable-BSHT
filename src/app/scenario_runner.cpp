#include "app/scenario_runner.h"

#include "godot/rng.h"
#include "scripts/BlinkerBranch.h"
#include "scripts/CameraManager.h"
#include "scripts/ItemAmounts.h"
#include "scripts/ItemManager.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/TextShaker.h"
#include "scripts/EndlessMode.h"
#include "scripts/IntroManager.h"
#include "scripts/intro_skip.h"
#include "scripts/resources.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace app {

using namespace core::json;

std::string ReadScenarioFile(const char* path) {
    std::string text;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return text;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    return text;
}

bool ScenarioRunner::Parse(std::string json) {
    json_ = std::move(json);
    const char* b = json_.data();
    const char* e = b + json_.size();
    name_ = ReadStringTopLevelDecoded(b, e, "name");
    scene_ = ReadStringTopLevelDecoded(b, e, "scene");
    if (scene_.empty()) scene_ = "main";
    gatePath_ = ReadStringTopLevelDecoded(b, e, "wait_for_visible");
    start_ = ReadIntTopLevel(b, e, "start_frame", 3);
    after_ = ReadIntTopLevel(b, e, "after", 90);
    frame_ = start_;
    const char* tb = nullptr;
    const char* te = nullptr;
    steps_.clear();
    if (FindArrayTopLevel(b, e, "timeline", &tb, &te)) SplitObjects(tb, te, steps_);
    last_ = 0;
    for (const Span& s : steps_) last_ = std::max(last_, ReadIntTopLevel(s.first, s.second, "at", 0));
    std::vector<double> shots;
    if (FindArrayTopLevel(b, e, "shots", &tb, &te)) ReadNumberArray(tb, te, shots);
    shots_.clear();
    for (double s : shots) shots_.push_back(static_cast<int>(s));
    return !gatePath_.empty();
}

// The start frame: what tools/godot/dump_state.gd does at the same frame.
void ScenarioRunner::Seed(godot::SceneTree& tree) {
    const char* b = json_.data();
    const char* e = b + json_.size();
    if (scene_ == "menu" || scene_ == "heaven") {
        // The menu runs itself from _ready (MenuManager.Intro); the scenario
        // only pins the RNG, as dump_state.gd _step_menu does. No particles
        // in menu.tscn, so the windowed renderer draws nothing from it.
        godot::seed(static_cast<uint64_t>(ReadNumberTopLevel(b, e, "seed", 0.0)));
        return;
    }
    auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    auto* cm = godot::script_cast<scripts::CameraManager>(tree.get_node("standalone managers/camera manager"));
    if (!rm || !cm || !rm->playerData) return;
    godot::seed(static_cast<uint64_t>(ReadNumberTopLevel(b, e, "seed", 0.0)));
    if (scene_ == "intro") {
        // main.tscn with the intro RUNNING (dump_state.gd _step_intro): only
        // the PlayerData fields the scenario names are set; no skip, no batch.
        const char* pb = nullptr;
        const char* pe = nullptr;
        if (FindObject(b, e, "player", &pb, &pe)) {
            scripts::PlayerData& pd = *rm->playerData;
            if (HasKey(pb, pe, "entering_from_death")) {
                pd.playerEnteringFromDeath = ReadBoolTopLevel(pb, pe, "entering_from_death", false);
            }
            if (HasKey(pb, pe, "signed")) pd.hasSignedWaiver = ReadBoolTopLevel(pb, pe, "signed", false);
            if (HasKey(pb, pe, "name")) pd.playername = ReadStringTopLevelDecoded(pb, pe, "name");
            if (HasKey(pb, pe, "batch")) pd.currentBatchIndex = ReadIntTopLevel(pb, pe, "batch", 0);
        }
        tree.set_particle_rng(!steps_.empty());
        return;
    }
    // A "waiver" scenario leaves it unsigned: the batch asks for it.
    rm->playerData->hasSignedWaiver = !ReadBoolTopLevel(b, e, "waiver", false);
    rm->playerData->currentBatchIndex = ReadIntTopLevel(b, e, "batch", 0);
    for (const scripts::CameraSocket* s : cm->socketArray) {
        if (s && s->socketName == "home") {
            cm->cam->set_position(s->pos);
            cm->cam->set_rotation_degrees(s->rot);
            cm->cam->set_fov(static_cast<float>(s->fov));
        }
    }
    // The intro: dump_state.gd frees the intro manager and hides its blackout
    // at this frame; the scenario starts at the table.
    scripts::SkipIntro(tree);
    // quiet_rng: stop everything else that draws from the global RNG (the
    // BlinkerBranch loops and TextShaker), as dump_state.gd's _quiet_rng does.
    if (ReadBoolTopLevel(b, e, "quiet_rng", false)) {
        for (godot::Node* n : tree.nodes_in_tree_order()) {
            if (auto* blinker = godot::script_cast<scripts::BlinkerBranch>(n)) blinker->looping = false;
            if (auto* shaker = godot::script_cast<scripts::TextShaker>(n)) shaker->looping = false;
        }
    }
    // A scenario with a timeline is referenced by the windowed Godot binary
    // (tools/godot/make_references.sh), whose renderer draws the global RNG
    // whenever a particle system restarts (godot/particles.h). The headless
    // binary's dummy renderer never does.
    tree.set_particle_rng(!steps_.empty());

    if (scene_ == "ending") {
        // ending_*: the table, then the last batch cleared (dump_state.gd
        // _step_ending): EndMainBatch at index 2 rolls the briefcase machine in.
        const char* pb = nullptr;
        const char* pe = nullptr;
        if (FindObject(b, e, "player", &pb, &pe)) {
            scripts::PlayerData& pd = *rm->playerData;
            pd.playername = ReadStringTopLevelDecoded(pb, pe, "name");
            pd.stat_shotsFired = ReadIntTopLevel(pb, pe, "shots", 0);
            pd.stat_shellsEjected = ReadIntTopLevel(pb, pe, "shells", 0);
            pd.stat_doorsKicked = ReadIntTopLevel(pb, pe, "doors", 0);
            pd.stat_cigSmoked = ReadIntTopLevel(pb, pe, "cigs", 0);
            pd.stat_beerDrank = ReadIntTopLevel(pb, pe, "beer", 0);
        }
        if (ReadStringTopLevelDecoded(b, e, "ending") == "don") {
            // The pills' SetupEndless, a fixed run time, batch 3 cleared:
            // EndMainBatch's endless branch starts BeginScoreLerp.
            if (auto* endless = godot::script_cast<scripts::Endless>(tree.get_node("standalone managers/endless mode"))) {
                endless->SetupEndless();
            }
            rm->counting = false;
            rm->initial_time = ReadNumberTopLevel(b, e, "initial_time", 0.0);
        }
        if (ReadStringTopLevelDecoded(b, e, "ending") == "true_death") {
            // DeathManager.MainDeathRoutine's heaven branch.
            rm->wireIsCut_player = true;
            rm->health_player = 0;
            tree.spawn(rm->OutOfHealth("player"));
            return;
        }
        rm->playerData->currentBatchIndex = 2;
        tree.spawn(rm->EndMainBatch());
        return;
    }
    tree.spawn(rm->MainBatchSetup(true));
}

ScenarioRunner::Step ScenarioRunner::BeginFrame(godot::SceneTree& tree, godot::Input& input) {
    if (!seeded_) {
        seeded_ = true;
        Seed(tree);
        return Step::Seeded;
    }
    if (zero_ < 0) {
        // In tree: the gate is `visible` at load, hidden only by its parent.
        const auto* gate = tree.get_node_as<godot::Control>(gatePath_);
        if (!gate || !gate->is_visible_in_tree()) return Step::Waiting;
        zero_ = frame_;
        std::printf("player turn reached at frame %d\n", frame_);
        // don_items: the Double or Nothing item pool (EndlessMode.gd:25,
        // amount_active = amount_don) without endless mode itself, the only
        // way a scenario reaches the four items the base campaign never deals.
        // At the gate, not the start: MainBatchSetup parses the base amounts
        // only after awaiting RoundIndicator, and no item is drawn before it.
        const char* b = json_.data();
        const char* e = b + json_.size();
        auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
        if (ReadBoolTopLevel(b, e, "don_items", false) && rm && rm->itemManager && rm->itemManager->amounts) {
            for (scripts::AmountResource* res : rm->itemManager->amounts->array_amounts) {
                res->amount_active = res->amount_don;
            }
        }
        // opponent_health: the dealer's health at the gate, so one live shot
        // can end him. The only way a scenario reaches what runs on a kill
        // (BloodSplatter, the time-scale shift) without playing a round.
        const int opponentHealth = ReadIntTopLevel(b, e, "opponent_health", -1);
        if (opponentHealth >= 0 && rm) rm->health_opponent = opponentHealth;
        // first_shell: "live" or "blank" in the chamber at the gate.
        const std::string firstShell = ReadStringTopLevelDecoded(b, e, "first_shell");
        if (!firstShell.empty() && rm && rm->shellSpawner && !rm->shellSpawner->sequenceArray.empty()) {
            rm->shellSpawner->sequenceArray[0] = firstShell;
        }
    }
    // crt_at: the screen's interaction at that frame, as dump_state.gd
    // _step_intro calls IntroManager.Interaction_CRT.
    if (scene_ == "intro") {
        const char* b = json_.data();
        const char* e = b + json_.size();
        if (ReadIntTopLevel(b, e, "crt_at", -1) == frame_ - zero_) {
            if (auto* im = godot::script_cast<scripts::IntroManager>(tree.get_node("standalone managers/intro manager"))) {
                tree.spawn(im->Interaction_CRT(), im->get_node_owner());
            }
        }
    }
    // Godot's frame: last frame's parsed events reach _input now, then the
    // physics step (which frees what a timer queued), then MainLoop::process.
    input.flush_buffered_events();
    tree.physics_frame();
    return Step::Running;
}

bool ScenarioRunner::EndFrame(godot::Input& input) {
    const int now = frame_++;
    if (zero_ < 0) return true;
    const int atNow = now - zero_;
    if (atNow >= last_ + after_) return false;
    for (const Span& s : steps_) {
        if (ReadIntTopLevel(s.first, s.second, "at", -1) != atNow) continue;
        const std::string what = ReadStringTopLevelDecoded(s.first, s.second, "event");
        if (what == "key") {
            auto k = std::make_shared<godot::InputEventKey>();
            k->keycode = ReadIntTopLevel(s.first, s.second, "keycode", 0);
            k->pressed = ReadBoolTopLevel(s.first, s.second, "pressed", true);
            k->device = 0;
            input.parse_input_event(k);
        } else if (what == "touch") {
            auto t = std::make_shared<godot::InputEventScreenTouch>();
            t->index = ReadIntTopLevel(s.first, s.second, "index", 0);
            t->position = {static_cast<float>(ReadNumberTopLevel(s.first, s.second, "x", 0.0)),
                           static_cast<float>(ReadNumberTopLevel(s.first, s.second, "y", 0.0))};
            t->pressed = ReadBoolTopLevel(s.first, s.second, "pressed", true);
            input.parse_input_event(t);
            touchAt_[t->index] = t->position;
        } else if (what == "drag") {
            auto d = std::make_shared<godot::InputEventScreenDrag>();
            d->index = ReadIntTopLevel(s.first, s.second, "index", 0);
            d->position = {static_cast<float>(ReadNumberTopLevel(s.first, s.second, "x", 0.0)),
                           static_cast<float>(ReadNumberTopLevel(s.first, s.second, "y", 0.0))};
            const godot::Vector2 was = touchAt_.count(d->index) ? touchAt_[d->index] : d->position;
            d->relative = {d->position.x - was.x, d->position.y - was.y};
            input.parse_input_event(d);
            touchAt_[d->index] = d->position;
        }
    }
    return true;
}

}  // namespace app
