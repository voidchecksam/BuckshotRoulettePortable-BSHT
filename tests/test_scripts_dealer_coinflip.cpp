// The base-game dealer is random, and that is correct. Outside endless,
// DealerIntelligence.CoinFlip() is randi_range(0, 1), a true 50/50. The odds
// weighting and FigureOutShell's deduction both exist only in Double or
// Nothing. The dealer can look "too dumb"; making him smarter would be a
// balance change, not a bug fix. Pinned on the translated script.
#include "godot/node.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/RoundManager.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

template <class T>
T* Attach(godot::Node* node) {
    auto script = std::make_unique<T>();
    T* raw = script.get();
    node->set_script(std::move(script));
    return raw;
}

// CoinFlip reads nothing but roundManager->endless, so the tree is two nodes.
struct CoinFlipTree {
    godot::SceneTree t;
    scripts::DealerIntelligence* dealer = nullptr;
    scripts::RoundManager* round = nullptr;

    explicit CoinFlipTree(bool endless) {
        godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
        dealer = Attach<scripts::DealerIntelligence>(
            t.add_node(std::make_unique<godot::Node>("dealer intelligence"), root));
        round = Attach<scripts::RoundManager>(
            t.add_node(std::make_unique<godot::Node>("round manager"), root));
        round->endless = endless;
        dealer->roundManager = round;
    }
};

}  // namespace

TEST(DealerIntelligenceCoinFlip, BaseGameIsGenuinelyRandom) {
    CoinFlipTree tree(/*endless=*/false);
    godot::seed(20260921);
    int shootsPlayer = 0;
    const int trials = 4000;
    for (int i = 0; i < trials; ++i) {
        if (tree.dealer->CoinFlip() == 1) ++shootsPlayer;
    }
    const double rate = static_cast<double>(shootsPlayer) / trials;
    EXPECT_NEAR(rate, 0.50, 0.03) << "the base-game dealer must not weight by odds";
}

TEST(DealerIntelligenceCoinFlip, BaseGameDrawsExactlyOneNumberPerFlip) {
    // Each flip must consume the stream exactly once, or every later draw (the
    // shells, the items) stops matching Godot's.
    CoinFlipTree tree(/*endless=*/false);
    godot::seed(1234);
    (void)tree.dealer->CoinFlip();
    (void)tree.dealer->CoinFlip();
    const int64_t afterTwoFlips = godot::randi_range(0, 1 << 30);

    godot::seed(1234);
    (void)godot::randi_range(0, 1);
    (void)godot::randi_range(0, 1);
    EXPECT_EQ(godot::randi_range(0, 1 << 30), afterTwoFlips);
}
