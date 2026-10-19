// Scripts on nodes, create_timer, tr, ease and shuffle: the runtime the
// translated GDScript stands on. Engine rules are transcribed from Godot
// 4.1.4-stable and cited where they are implemented.
#include "godot/array_ops.h"
#include "godot/control.h"
#include "godot/math_funcs.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "godot/script.h"
#include "godot/translation.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Recorder : godot::Script {
    static constexpr const char* kClassName = "Recorder";
    const char* get_class_name() const override { return kClassName; }

    std::vector<std::string>* log = nullptr;
    std::string tag;

    void _ready() override { log->push_back("ready:" + tag); }
    bool has_process() const override { return true; }
    void _process(double delta) override {
        log->push_back("process:" + tag + ":" + std::to_string(static_cast<int>(delta * 1000.0 + 0.5)));
    }
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override {
        if (method != "Mark") return false;
        log->push_back("mark:" + args.at(0).s);
        return true;
    }
    seq::Task Wait(double seconds) {
        co_await get_tree()->create_timer(seconds);
        log->push_back("waited:" + tag);
    }
};

godot::Node* AddScripted(godot::SceneTree& tree, const char* name, godot::Node* parent,
                         std::vector<std::string>& log) {
    auto node = std::make_unique<godot::Node>(name);
    auto script = std::make_unique<Recorder>();
    script->log = &log;
    script->tag = name;
    node->set_script(std::move(script));
    return tree.add_node(std::move(node), parent);
}

bool Contains(const std::vector<std::string>& log, const std::string& s) {
    return std::find(log.begin(), log.end(), s) != log.end();
}

}  // namespace

TEST(GodotScript, ReadyRunsChildrenFirstAndEnablesProcess) {
    godot::SceneTree tree;
    std::vector<std::string> log;
    godot::Node* a = AddScripted(tree, "A", nullptr, log);
    AddScripted(tree, "B", a, log);

    tree.ready_scripts();

    EXPECT_EQ(log, (std::vector<std::string>{"ready:B", "ready:A"}));
    EXPECT_TRUE(a->is_processing());
    log.clear();
    tree.process_frame(1.0 / 60.0);
    EXPECT_EQ(log, (std::vector<std::string>{"process:A:17", "process:B:17"}));
}

TEST(GodotScript, ReadyScriptsRunsEachScriptOnce) {
    godot::SceneTree tree;
    std::vector<std::string> log;
    AddScripted(tree, "A", nullptr, log);

    tree.ready_scripts();
    tree.ready_scripts();

    EXPECT_EQ(log, (std::vector<std::string>{"ready:A"}));
}

TEST(GodotScript, CallReachesTheScriptAndUnknownMethodsAreRefused) {
    godot::SceneTree tree;
    std::vector<std::string> log;
    godot::Node* a = AddScripted(tree, "A", nullptr, log);
    godot::Variant v;
    v.kind = assets::ValueKind::Str;
    v.s = "x";

    EXPECT_TRUE(a->call("Mark", {v}));
    EXPECT_FALSE(a->call("Nope", {}));
    EXPECT_EQ(log.back(), "mark:x");
}

TEST(GodotScript, ScriptCastChecksTheClassName) {
    godot::SceneTree tree;
    std::vector<std::string> log;
    godot::Node* a = AddScripted(tree, "A", nullptr, log);
    godot::Node plain("plain");

    EXPECT_NE(godot::script_cast<Recorder>(a), nullptr);
    EXPECT_EQ(godot::script_cast<Recorder>(&plain), nullptr);
    EXPECT_EQ(godot::script_cast<Recorder>(static_cast<godot::Node*>(nullptr)), nullptr);
}

TEST(GodotScript, CreateTimerHonoursTimeScale) {
    godot::SceneTree tree;
    std::vector<std::string> log;
    godot::Node* a = AddScripted(tree, "A", nullptr, log);
    tree.set_time_scale(0.5);

    tree.spawn(godot::script_cast<Recorder>(a)->Wait(0.1));
    for (int i = 0; i < 11; ++i) tree.process_frame(1.0 / 60.0);   // 0.0917 s scaled
    EXPECT_FALSE(Contains(log, "waited:A"));
    for (int i = 0; i < 2; ++i) tree.process_frame(1.0 / 60.0);    // 0.108 s scaled
    EXPECT_TRUE(Contains(log, "waited:A"));
}

TEST(GodotScript, ProcessDeltaTimeIsTheScaledFrameDelta) {
    godot::SceneTree tree;
    tree.set_time_scale(0.5);
    tree.process_frame(0.1);
    EXPECT_DOUBLE_EQ(tree.get_process_delta_time(), 0.05);
}

TEST(GodotScript, TheRegistryCreatesByScriptFileName) {
    godot::register_script_class("RecorderFile", [] {
        return std::unique_ptr<godot::Script>(std::make_unique<Recorder>());
    });
    const std::unique_ptr<godot::Script> s = godot::create_script("RecorderFile");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s->get_class_name(), "Recorder");
    EXPECT_EQ(godot::create_script("NoSuchScript"), nullptr);
}

// core/math/math_funcs.cpp:95 at 4.1.4-stable.
TEST(GodotMath, EaseMatchesGodot) {
    EXPECT_NEAR(godot::ease(0.5, 0.2), 1.0 - std::pow(0.5, 1.0 / 0.2), 1e-12);
    EXPECT_NEAR(godot::ease(0.5, 2.0), 0.25, 1e-12);
    EXPECT_NEAR(godot::ease(0.25, -2.0), std::pow(0.5, 2.0) * 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(godot::ease(-1.0, 0.2), 0.0);
    EXPECT_DOUBLE_EQ(godot::ease(2.0, 0.2), 1.0);
    EXPECT_DOUBLE_EQ(godot::ease(0.5, 0.0), 0.0);
}

TEST(GodotMath, DecibelsMatchGodot) {
    EXPECT_NEAR(godot::linear_to_db(1.0), 0.0, 1e-12);
    EXPECT_NEAR(godot::db_to_linear(godot::linear_to_db(0.5)), 0.5, 1e-12);
}

// core/variant/array.cpp:624 at 4.1.4-stable.
TEST(GodotArray, ShuffleConsumesTheGlobalRngLikeGodot) {
    godot::seed(12345);
    std::vector<std::string> v{"live", "blank", "blank", "live", "blank"};
    godot::shuffle(v);

    godot::seed(12345);
    std::vector<std::string> w{"live", "blank", "blank", "live", "blank"};
    for (int i = static_cast<int>(w.size()) - 1; i >= 1; --i) {
        const int j = static_cast<int>(godot::default_rand().rand() % static_cast<uint32_t>(i + 1));
        std::swap(w[static_cast<std::size_t>(i)], w[static_cast<std::size_t>(j)]);
    }
    EXPECT_EQ(v, w);
}

TEST(GodotArray, ShuffleOfOneElementDrawsNothing) {
    godot::seed(1);
    std::vector<int> one{7};
    godot::shuffle(one);
    const uint32_t next = godot::default_rand().rand();
    godot::seed(1);
    EXPECT_EQ(godot::default_rand().rand(), next);
}

TEST(GodotString, FormatUpperAndStr) {
    EXPECT_EQ(godot::format_s("A %s B", "x"), "A x B");
    EXPECT_EQ(godot::to_upper("abC1"), "ABC1");
    EXPECT_EQ(godot::str(42), "42");
}

TEST(GodotTranslation, TrReadsTheTableAndFallsBackToTheKey) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/dialogue_en.json");
    ASSERT_TRUE(godot::load_translations(BSHT_SOURCE_DIR "/assets/extracted/dialogue_en.json"));
    const std::string& drill = godot::tr("DRILL");
    EXPECT_FALSE(drill.empty());
    EXPECT_NE(drill, "DRILL");
    EXPECT_EQ(godot::tr("NO SUCH KEY"), "NO SUCH KEY");
    EXPECT_FALSE(godot::load_translations(BSHT_SOURCE_DIR "/assets/extracted/no_such_file.json"));
}

// label.cpp:769 set_text: xl_text = atr(p_string). A Label draws its text
// translated: PickupIndicator writes the key "SHOTGUN DESC" and the screen
// shows the sentence. get_text() stays the key, as in Godot.
TEST(GodotTranslation, ALabelDrawsItsTextTranslated) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/dialogue_en.json");
    ASSERT_TRUE(godot::load_translations(BSHT_SOURCE_DIR "/assets/extracted/dialogue_en.json"));
    godot::Label label("item description");
    label.set_text("DRILL");
    EXPECT_EQ(label.get_text(), "DRILL");
    EXPECT_EQ(label.get_xl_text(), godot::tr("DRILL"));
    EXPECT_NE(label.get_xl_text(), "DRILL");
    label.set_text("not a key");
    EXPECT_EQ(label.get_xl_text(), "not a key");

    // Text set before the table loads is translated once it does (label.cpp:380).
    ASSERT_FALSE(godot::load_translations(BSHT_SOURCE_DIR "/assets/extracted/no_such_file.json"));
    label.set_text("DRILL");
    EXPECT_EQ(label.get_xl_text(), "DRILL") << "no table: the key";
    ASSERT_TRUE(godot::load_translations(BSHT_SOURCE_DIR "/assets/extracted/dialogue_en.json"));
    EXPECT_EQ(label.get_xl_text(), godot::tr("DRILL"));
}
