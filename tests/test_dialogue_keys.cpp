// Every item's name and description is a translation key (Godot's Label draws
// atr(text)). The keys come from main.tscn's pickup indicators and from the
// item scenes (runtime_item_*.json); without the second, CIGARETTE DESC and
// BEER DESC draw as raw keys.
#include "test_data.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string ReadAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::set<std::string> ItemKeys() {
    std::set<std::string> keys;
    // PickupIndicator's two exports, side by side. InteractionBranch also has
    // an itemName, but it is an identifier ("cigarettes"), never drawn.
    const std::regex pair(R"re("itemName"\s*:\s*"([^"]+)"\s*,\s*"itemDescription"\s*:\s*"([^"]+)")re");
    for (const fs::directory_entry& e : fs::directory_iterator(BSHT_SOURCE_DIR "/assets/extracted")) {
        const std::string name = e.path().filename().string();
        if (name.rfind("runtime_item_", 0) != 0 || e.path().extension() != ".json") continue;
        const std::string text = ReadAll(e.path());
        for (auto it = std::sregex_iterator(text.begin(), text.end(), pair); it != std::sregex_iterator(); ++it) {
            keys.insert((*it)[1].str());
            keys.insert((*it)[2].str());
        }
    }
    return keys;
}

TEST(DialogueKeys, EveryItemsNameAndDescriptionIsInEveryLocale) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/dialogue_en.json");
    const std::set<std::string> keys = ItemKeys();
    ASSERT_GE(keys.size(), 9u);   // nine items, a description each

    int locales = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(BSHT_SOURCE_DIR "/assets/extracted")) {
        const std::string name = e.path().filename().string();
        if (name.rfind("dialogue_", 0) != 0 || e.path().extension() != ".json") continue;
        ++locales;
        const std::string table = ReadAll(e.path());
        for (const std::string& k : keys) {
            EXPECT_NE(table.find("\"" + k + "\":"), std::string::npos) << name << " lacks " << k;
        }
    }
    EXPECT_GE(locales, 17);
}

}  // namespace
