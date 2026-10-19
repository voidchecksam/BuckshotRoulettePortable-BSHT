#include "godot/print.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace godot {

namespace {

std::function<void(const std::string&)>& Sink() {
    static std::function<void(const std::string&)> sink;
    return sink;
}

}  // namespace

void set_print_sink(std::function<void(const std::string&)> sink) { Sink() = std::move(sink); }

void print_line(const std::string& line) {
    if (Sink()) Sink()(line);
}

std::string print_str(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        return std::to_string(static_cast<long long>(v));
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.14g", v);
    return buf;
}

}  // namespace godot
