// Starts the game on a bound main.tscn tree.
//
// The table runs from the translated scripts in the godot:: tree: RoundManager,
// ShellSpawner, ShotgunShooting, DealerIntelligence and DeathManager drive the
// turn between them. Session only starts it: the restroom intro on a player's
// boot, or straight at the table for a diagnostic.
//
// Session links no platform, so a whole table can be driven in a test.
#pragma once

#include <functional>
#include <string>

namespace godot { class SceneTree; }

namespace game {

class Session {
public:
    using LogSink = std::function<void(const std::string&)>;

    // `tree` has the translated scripts bound and readied. False when it does
    // not. Starts RoundManager.BeginMainGame(), or lets the intro do it.
    bool Start(godot::SceneTree* tree);

    void SetLog(LogSink sink) { log_ = std::move(sink); }

    // --batch N. Seeds playerData.currentBatchIndex at Start; 0 leaves it alone.
    void SetStartBatch(int batch) { startBatch_ = batch; }
    // A player's boot: IntroManager lifts the blackout, loads the track and
    // calls BeginMainGame itself at the end of the walk. Off (diagnostics,
    // scenarios): the intro is skipped and the batch starts now.
    void SetPlaysIntro(bool on) { playsIntro_ = on; }

private:
    bool    playsIntro_ = false;
    LogSink log_;
    int     startBatch_ = 0;
};

}  // namespace game
