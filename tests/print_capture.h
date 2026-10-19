// Collects what the scripts print() while it lives (godot::set_print_sink).
#pragma once

#include "godot/print.h"

#include <algorithm>
#include <string>
#include <vector>

namespace testing_support {

struct PrintCapture {
    std::vector<std::string> lines;

    PrintCapture() {
        godot::set_print_sink([this](const std::string& line) { lines.push_back(line); });
    }
    ~PrintCapture() { godot::set_print_sink(nullptr); }
    PrintCapture(const PrintCapture&) = delete;
    PrintCapture& operator=(const PrintCapture&) = delete;

    bool Has(const std::string& line) const { return std::find(lines.begin(), lines.end(), line) != lines.end(); }
};

}  // namespace testing_support
