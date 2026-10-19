// Holding a scenario shot's frame before it is read. The scenario stops
// advancing on a shot's frame and the same state is redrawn until the shot is
// taken:
//   - at least `minFrames` (Vita3K copies a rendered surface back to emulated
//     memory late, so glReadPixels read zeros on the shot's own frame);
//   - with a handshake (maxFrames > minFrames), until the grabber releases it
//     (tools/vita3k/run.sh grabs the emulator's window, which can take
//     1-2.5 s, longer than a fixed hold), or until `maxFrames`, which
//     TimedOut() then reports so the shot is not trusted.
// ShotHold(0) (the desktop) takes every shot on its own frame.
//
//   ShotHold hold(30, 1200);
//   if (hold.Holding()) { shot = hold.Tick(GrabberReleased(hold.Held())); return; }   // no scenario step
//   ... step the scenario ...; shot = hold.Request(shotOfThisFrame);
#pragma once

namespace app {

class ShotHold {
public:
    explicit ShotHold(int minFrames, int maxFrames = 0)
        : min_(minFrames > 0 ? minFrames : 0), max_(maxFrames > min_ ? maxFrames : min_) {}

    // On a frame the scenario stepped: the shot to take NOW (-1: none, or held).
    int Request(int shot) {
        if (shot < 0 || max_ == 0) return shot;
        held_ = shot;
        elapsed_ = 0;
        holding_ = true;
        timedOut_ = false;
        return -1;
    }

    bool Holding() const { return holding_; }
    int Held() const { return holding_ ? held_ : -1; }

    // On a held frame (no scenario step): the held shot on the frame the hold
    // ends, else -1. `released`: the grabber has the frame (always true without
    // a handshake).
    int Tick(bool released = true) {
        if (!holding_) return -1;
        ++elapsed_;
        if (elapsed_ < min_) return -1;
        const bool expired = elapsed_ >= max_;
        if (!released && !expired) return -1;
        holding_ = false;
        timedOut_ = !released;
        return held_;
    }

    // The last hold ended at maxFrames without a release: its shot is suspect.
    bool TimedOut() const { return timedOut_; }

private:
    int min_;
    int max_;
    int held_ = -1;
    int elapsed_ = 0;
    bool holding_ = false;
    bool timedOut_ = false;
};

}  // namespace app
