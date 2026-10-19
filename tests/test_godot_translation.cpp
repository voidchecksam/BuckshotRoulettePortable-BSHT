// Every language's table. TranslationServer.set_locale switches tr() to that
// locale's table (export_dialogue.gd wrote what Godot displays per locale,
// fallback included). A key no table resolves returns the key itself, as
// Godot's tr() does.
#include "godot/translation.h"

#include <string>

#include "test_data.h"

#include <gtest/gtest.h>

namespace {

const std::string kRoot = std::string(BSHT_SOURCE_DIR) + "/assets/extracted";

struct Translation : ::testing::Test {
    void SetUp() override {
        BSHT_NEEDS_FILE(kRoot + "/dialogue_en.json");
        godot::set_translation_root(kRoot);
        ASSERT_TRUE(godot::set_locale("EN"));
    }
    void TearDown() override { godot::set_locale("EN"); }
};

TEST_F(Translation, FileCodeIsTheSheetNameLowercased) {
    EXPECT_EQ(godot::locale_file_code("EN"), "en");
    EXPECT_EQ(godot::locale_file_code("ES LATAM"), "es_latam");
    EXPECT_EQ(godot::locale_file_code("ZHS"), "zhs");
}

TEST_F(Translation, SetLocaleSwitchesTheTable) {
    const std::string en = godot::tr("LIVEROUND");
    ASSERT_TRUE(godot::set_locale("FR"));
    EXPECT_EQ(godot::get_locale(), "FR");
    EXPECT_NE(godot::tr("LIVEROUND"), en);
    ASSERT_TRUE(godot::set_locale("EN"));
    EXPECT_EQ(godot::tr("LIVEROUND"), en);
    EXPECT_EQ(godot::get_locale(), "EN");
}

TEST_F(Translation, SetLocaleBumpsTheGeneration) {
    const unsigned before = godot::translation_generation();
    ASSERT_TRUE(godot::set_locale("RU"));
    EXPECT_NE(godot::translation_generation(), before);
}

// Measured in Godot 4.1.4: after set_locale("TU") get_locale() is "TU" and
// tr("START") is "START". No table matches, so every key comes back as itself;
// the previous language does not stay.
TEST_F(Translation, AnUnknownLocaleIsSetAndEveryKeyComesBackAsItself) {
    ASSERT_TRUE(godot::set_locale("DE"));
    EXPECT_FALSE(godot::set_locale("TU"));   // false: no table for it
    EXPECT_EQ(godot::get_locale(), "TU");
    EXPECT_EQ(godot::tr("START"), "START");
    EXPECT_EQ(godot::tr("LIVEROUND"), "LIVEROUND");
}

TEST_F(Translation, AKeyNoTableHoldsReturnsTheKey) {
    // Every v2.2 locale has all 113 keys, so only an unknown key shows as itself.
    ASSERT_TRUE(godot::set_locale("ES LATAM"));
    EXPECT_EQ(godot::tr("NOT A KEY AT ALL"), "NOT A KEY AT ALL");
}

TEST_F(Translation, EveryOneOfTheSeventeenLoads) {
    for (const char* code : {"BR", "DE", "EE", "EN", "ES LATAM", "ES", "FR", "IT", "JA", "KO", "PL", "PT",
                             "RU", "TR", "UA", "ZHS", "ZHT"}) {
        EXPECT_TRUE(godot::set_locale(code)) << code;
        EXPECT_NE(godot::tr("START"), "") << code;
    }
}

}  // namespace

// GDScript `"..." % [args]` (ustring.cpp String::sprintf): each %s takes the
// next argument, %% is a percent sign, other text is kept.
TEST(GodotFormat, PercentSTakesTheArgumentsInOrder) {
    EXPECT_EQ(godot::format("WELL PLAYED, %s!", {"ABE"}), "WELL PLAYED, ABE!");
    EXPECT_EQ(godot::format("PRESS '%s' TO LEAVE ...", {"Escape"}), "PRESS 'Escape' TO LEAVE ...");
    EXPECT_EQ(godot::format("%s and %s", {"a", "b"}), "a and b");
    EXPECT_EQ(godot::format("100%%", {}), "100%");
    EXPECT_EQ(godot::format("no args", {}), "no args");
}

// The port's own line after a message, in every locale: the port's credit at
// the end of the main credits, just above the Critical Reflex logo. It
// survives a locale change and bumps the generation, so a Label re-translates.
namespace {

struct TranslationSuffix : Translation {
    void TearDown() override {
        godot::set_message_suffix("MAIN CREDITS", "");
        Translation::TearDown();
    }
};

TEST_F(TranslationSuffix, ASuffixEndsTheMessageInEveryLocale) {
    const std::string en = godot::tr("MAIN CREDITS");
    const unsigned before = godot::translation_generation();
    godot::set_message_suffix("MAIN CREDITS", "\n\nPORT LINE");

    EXPECT_NE(godot::translation_generation(), before);
    EXPECT_EQ(godot::tr("MAIN CREDITS"), en + "\n\nPORT LINE");
    ASSERT_TRUE(godot::set_locale("FR"));
    const std::string fr = godot::tr("MAIN CREDITS");
    ASSERT_GE(fr.size(), std::string("\n\nPORT LINE").size());
    EXPECT_EQ(fr.substr(fr.size() - std::string("\n\nPORT LINE").size()), "\n\nPORT LINE");
    EXPECT_NE(fr, en + "\n\nPORT LINE");
}

TEST_F(TranslationSuffix, OtherKeysAndAnEmptySuffixAreUntouched) {
    const std::string live = godot::tr("LIVEROUND");
    const std::string credits = godot::tr("MAIN CREDITS");
    godot::set_message_suffix("MAIN CREDITS", "\n\nPORT LINE");
    EXPECT_EQ(godot::tr("LIVEROUND"), live);
    godot::set_message_suffix("MAIN CREDITS", "");
    EXPECT_EQ(godot::tr("MAIN CREDITS"), credits);
}

}  // namespace
