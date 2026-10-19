#include "parity/probes.h"

#include "assets/animation.h"
#include "core/json_lite.h"
#include "godot/animation_player.h"
#include "godot/scene_tree.h"

#include <cmath>
#include <cstdio>
#include <memory>

namespace parity {
namespace {

struct ProbeLog {
    std::vector<std::string> events;
    uint64_t frame = 0;   // 0 during _ready, as dump_state.gd's `frame`
    void add(const std::string& s) { events.push_back("f" + std::to_string(frame) + ":" + s); }
};

class Probe;
seq::Task Wait(Probe* self, std::string tag, double seconds);

class Probe : public godot::Node {
public:
    // GDScript turns processing on for a script that defines _process.
    Probe(const char* name, ProbeLog* log) : godot::Node(name), log_(log) { set_process(true); }

    void _ready() override {
        log_->add("ready:" + get_name());
        if (get_name() == "A") get_tree()->spawn(Wait(this, "t40", 0.04));
    }

    void _process(double delta) override {
        if (get_name() == "A" || get_name() == "D") {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "process:%s:%.3f", get_name().c_str(), delta * 1000.0);
            log_->add(buf);
        }
        if (get_name() == "A" && log_->frame == 4) {
            get_tree()->set_time_scale(0.5);
            get_tree()->spawn(Wait(this, "t20", 0.02));
        }
    }

    ProbeLog* log_;
};

seq::Task Wait(Probe* self, std::string tag, double seconds) {
    co_await godot::Timer(seconds);
    self->log_->add("timer:" + tag);
    if (tag == "t40") {
        co_await godot::Timer(0.0);
        self->log_->add("timer:zero");
    }
}

struct Step {
    int frame = 0;
    std::string action;
    std::string anim;
    double time = 0.0;
    bool update = false;
};

// SemProbe from tools/godot/dump_state.gd.
class SemProbe : public godot::Node {
public:
    SemProbe(const char* name, ProbeLog* log, bool post)
        : godot::Node(name), log_(log), post_(post) {
        set_process(true);
    }

    void _process(double) override {
        if (post_) {
            State("post");
            return;
        }
        for (const Step& s : *steps) {
            if (s.frame != static_cast<int>(log_->frame)) continue;
            if (s.action == "play") ap->play(s.anim);
            else if (s.action == "stop") ap->stop();
            else if (s.action == "seek") ap->seek(s.time, s.update);
        }
        State("pre");
    }

    bool call(const std::string& method, const std::vector<assets::Value>& args) override {
        if (method != "mark") return false;
        log_->add("mark:" + (args.empty() ? std::string() : args[0].s));
        return true;
    }

    const std::vector<Step>* steps = nullptr;
    godot::AnimationPlayer* ap = nullptr;
    godot::Node3D* t = nullptr;

private:
    // GDScript "%.4f": String::sprintf writes the sign itself, then
    // String::num(|v|, 4) hands "%.4lf" to the C runtime Godot's Windows build
    // links, which rounds an exact tie away from zero: 1.15625 prints "1.1563".
    // The runtime this port links rounds ties to even ("1.1562"), as
    // parity_anim_play_semantics shows on the cubic scale track at f4.
    static std::string Fixed4(double v) {
        const double rounded = std::floor(std::fabs(v) * 10000.0 + 0.5) / 10000.0;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%.4f", std::signbit(v) ? "-" : "", rounded);
        return buf;
    }

    void State(const char* tag) {
        const std::string line = std::string(tag) + ":x=" + Fixed4(t->get_position().x) +
                                 ",sx=" + Fixed4(t->get_scale().x) + ",sy=" + Fixed4(t->get_scale().y) +
                                 ",vis=" + (t->is_visible() ? "1" : "0") +
                                 ",playing=" + (ap->is_playing() ? "1" : "0") +
                                 ",cur=" + ap->get_current_animation() +
                                 ",pos=" + Fixed4(ap->get_current_animation_position());
        log_->add(line);
    }

    ProbeLog* log_;
    bool post_;
};

std::vector<Step> ReadSteps(const char* b, const char* e) {
    using namespace core::json;
    std::vector<Step> steps;
    const char* sb = nullptr;
    const char* se = nullptr;
    if (!FindArrayTopLevel(b, e, "steps", &sb, &se)) return steps;
    std::vector<Span> objs;
    SplitObjects(sb, se, objs);
    for (const Span& o : objs) {
        Step s;
        s.frame = ReadIntTopLevel(o.first, o.second, "frame", 0);
        s.action = ReadStringTopLevel(o.first, o.second, "do");
        s.anim = ReadStringTopLevel(o.first, o.second, "anim");
        s.time = ReadNumberTopLevel(o.first, o.second, "time", 0.0);
        s.update = ReadBoolTopLevel(o.first, o.second, "update", false);
        steps.push_back(std::move(s));
    }
    return steps;
}

}  // namespace

std::vector<std::string> RunAnimPlaySemanticsProbe(const std::string& scenarioJson) {
    using namespace core::json;
    const char* b = scenarioJson.data();
    const char* e = b + scenarioJson.size();
    const char* ab = nullptr;
    const char* ae = nullptr;
    assets::AnimationLibrary lib;
    if (!FindObject(b, e, "animations", &ab, &ae) || !lib.LoadFromString(std::string(ab, ae).c_str()) ||
        lib.Players().empty()) {
        return {"scenario animations did not load"};
    }
    const std::vector<Step> steps = ReadSteps(b, e);
    const int frames = ReadIntTopLevel(b, e, "frames", 0);

    ProbeLog log;
    godot::SceneTree tree;
    auto* p = static_cast<SemProbe*>(tree.add_node(std::make_unique<SemProbe>("P", &log, false), nullptr));
    auto* ap = godot::cast_to<godot::AnimationPlayer>(
        tree.add_node(std::make_unique<godot::AnimationPlayer>("AP"), p));
    auto* t = godot::cast_to<godot::Node3D>(tree.add_node(std::make_unique<godot::Node3D>("T"), p));
    auto* z = static_cast<SemProbe*>(tree.add_node(std::make_unique<SemProbe>("Z", &log, true), p));
    ap->set_animations(&lib.Players()[0]);
    for (SemProbe* probe : {p, z}) {
        probe->steps = &steps;
        probe->ap = ap;
        probe->t = t;
    }
    tree.ready();
    for (int f = 1; f <= frames; ++f) {
        log.frame = static_cast<uint64_t>(f);
        tree.process_frame(1.0 / 60.0);
    }
    return log.events;
}

std::vector<std::string> RunFrameOrderProbe(int frames) {
    ProbeLog log;
    godot::SceneTree tree;
    // Godot adds A to an already-live root Window, which logs nothing; a plain
    // Node root plays that part here.
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
    godot::Node* a = tree.add_node(std::make_unique<Probe>("A", &log), root);
    godot::Node* b = tree.add_node(std::make_unique<Probe>("B", &log), a);
    tree.add_node(std::make_unique<Probe>("D", &log), b);
    tree.add_node(std::make_unique<Probe>("C", &log), a);
    tree.ready();
    for (int f = 1; f <= frames; ++f) {
        log.frame = static_cast<uint64_t>(f);
        tree.process_frame(1.0 / 60.0);
    }
    return log.events;
}

}  // namespace parity
