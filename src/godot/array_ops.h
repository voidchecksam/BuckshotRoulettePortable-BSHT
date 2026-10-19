// Array and String operations the translated scripts use, with Godot 4.1.4's
// semantics.
#pragma once

#include "godot/rng.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace godot {

// Array::shuffle (core/variant/array.cpp:624): Fisher-Yates from the top,
// j = Math::rand() % (i + 1) on the GLOBAL generator. Fewer than two elements
// draw nothing.
template <class T>
void shuffle(std::vector<T>& a) {
    const int n = static_cast<int>(a.size());
    if (n < 2) return;
    for (int i = n - 1; i >= 1; --i) {
        const int j = static_cast<int>(default_rand().rand() % static_cast<uint32_t>(i + 1));
        std::swap(a[static_cast<std::size_t>(j)], a[static_cast<std::size_t>(i)]);
    }
}

inline std::string str(int v) { return std::to_string(v); }

// String.to_upper. The strings this touches (player names, dialogue keys) are
// ASCII; PORT: non-ASCII case mapping is not implemented.
inline std::string to_upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

// `fmt % [arg]` with a single %s, the only form the scripts use.
inline std::string format_s(std::string_view fmt, std::string_view arg) {
    std::string out;
    out.reserve(fmt.size() + arg.size());
    bool done = false;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (!done && fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 's') {
            out.append(arg);
            ++i;
            done = true;
        } else {
            out.push_back(fmt[i]);
        }
    }
    return out;
}

}  // namespace godot
