// GDScript's print(): its arguments joined with no separator, one line. The
// app sets the sink (its log); with none, nothing is printed.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace godot {

void set_print_sink(std::function<void(const std::string&)> sink);
void print_line(const std::string& line);

// str() of one argument, as GDScript writes it: true/false, whole floats
// without a fraction.
std::string print_str(double v);
inline std::string print_str(std::string_view s) { return std::string(s); }
inline std::string print_str(const char* s) { return s ? std::string(s) : std::string(); }
inline std::string print_str(const std::string& s) { return s; }
inline std::string print_str(bool b) { return b ? "true" : "false"; }
template <class T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
std::string print_str(T v) {
    return std::to_string(v);
}
inline std::string print_str(float v) { return print_str(static_cast<double>(v)); }

template <class... Args>
void print(const Args&... args) {
    std::string line;
    (line += ... += print_str(args));
    print_line(line);
}

}  // namespace godot
