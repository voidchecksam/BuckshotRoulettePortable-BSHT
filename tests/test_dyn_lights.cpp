// Which lights the static bake takes. A reveal can leave an animated light
// burning (the zippo's flame at 18.4, 2.0 s into "player use cigarettes");
// baked, it would be a lamp that never goes out and light the zippo twice
// while it burns.
#include "assets/dyn_lights.h"

#include <gtest/gtest.h>

namespace {

TEST(DynLights, AnAnimatedLightIsNeverBakedWhateverItsPose) {
    for (std::string_view path : assets::kDynLightPaths) {
        EXPECT_TRUE(assets::IsDynLight(path)) << path;
        EXPECT_FALSE(assets::BakesAsStaticLight(path, true, 18.392f)) << path;
    }
}

TEST(DynLights, TheStaticBakeTakesVisibleBurningStillLights) {
    EXPECT_TRUE(assets::BakesAsStaticLight("light parent/light_main", true, 4.0f));
    EXPECT_FALSE(assets::BakesAsStaticLight("light parent/light_main", false, 4.0f)) << "hidden";
    EXPECT_FALSE(assets::BakesAsStaticLight("light parent/light_main", true, 0.0f)) << "off";
    EXPECT_FALSE(assets::BakesAsStaticLight(assets::kMovingLight, true, 4.1f)) << "lit per vertex at runtime";
}

// v2.2 renamed three of them with an "LS" suffix.
TEST(DynLights, TheChannelOrderIsTheShadersRgba) {
    EXPECT_EQ(assets::kDynLightPaths[0], "tabletop parent/shotgun main parent/light_muzzle flash LS");
    EXPECT_EQ(assets::kDynLightPaths[1], "light parent/light_barrel mask LS");
    EXPECT_EQ(assets::kDynLightPaths[2], "light parent/light_shotgun inspect mask LS");
    EXPECT_EQ(assets::kDynLightPaths[3], "player item interaction parent/zippo lighter parent2/light_zippo flame");
}

TEST(DynLights, TheV12NamesAreNotAnimatedLightsAnyMore) {
    EXPECT_FALSE(assets::IsDynLight("tabletop parent/shotgun main parent/light_muzzle flash"));
}

}  // namespace
