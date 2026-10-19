// GDScript's print(): the arguments joined with no separator, one line, to
// whatever sink the app set (its log).
#include "godot/print.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(GodotPrint, JoinsItsArgumentsAsGdscriptDoes) {
    std::vector<std::string> lines;
    godot::set_print_sink([&lines](const std::string& s) { lines.push_back(s); });
    godot::print("changing scene to: ", "main");
    godot::print("user current  hotfix: ", 6);
    godot::print("loaded ", true, " ", 1.5, " ", 2.0);
    godot::set_print_sink(nullptr);
    godot::print("not seen");
    EXPECT_EQ(lines, (std::vector<std::string>{"changing scene to: main", "user current  hotfix: 6",
                                                "loaded true 1.5 2"}));
}

}  // namespace
