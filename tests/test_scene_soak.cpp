// app/scene_soak: BSHT_SCENE_SOAK cycles menu <-> main on its own, so the heap
// can be read across many scene changes.
#include "app/scene_soak.h"

#include <gtest/gtest.h>

namespace {

TEST(SceneSoak, WaitsItsSecondsInEachScene) {
    app::SceneSoak soak(/*cycles=*/1, /*secondsPerScene=*/1.0f);

    EXPECT_EQ(soak.Next(0.5f, "menu"), "");
    EXPECT_EQ(soak.Next(0.6f, "menu"), "res://scenes/main.tscn");
}

TEST(SceneSoak, AlternatesAndStopsAfterItsCycles) {
    app::SceneSoak soak(/*cycles=*/2, /*secondsPerScene=*/1.0f);

    EXPECT_EQ(soak.Next(1.0f, "menu"), "res://scenes/main.tscn");
    EXPECT_EQ(soak.Next(1.0f, "main"), "res://scenes/menu.tscn");
    EXPECT_EQ(soak.Next(1.0f, "menu"), "res://scenes/main.tscn");
    EXPECT_EQ(soak.Next(1.0f, "main"), "res://scenes/menu.tscn");
    EXPECT_FALSE(soak.Done());

    EXPECT_EQ(soak.Next(1.0f, "menu"), "");   // four changes made: back at the menu, done
    EXPECT_TRUE(soak.Done());
}

TEST(SceneSoak, AnyOtherSceneGoesBackToTheMenu) {
    app::SceneSoak soak(1, 1.0f);

    EXPECT_EQ(soak.Next(1.0f, "heaven"), "res://scenes/menu.tscn");
}

}  // namespace
