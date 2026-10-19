// SDL2 key -> Godot 4.1.4 Key (core/os/keyboard.h). Desktop only.
//
// Godot's printable keys are their uppercase ASCII codes (KEY_A = 65,
// KEY_0 = 48, KEY_PERIOD = 46, KEY_COMMA = 44); special keys are
// SPECIAL | n (keyboard.h:45-64). The keycode is what the key produces on the
// active layout; the physical keycode is the key's position on a US layout,
// which SDL reports as the scancode, and Godot's physical `reset` / digit
// actions match on that.
#pragma once

#include "godot/input_event.h"

#include <SDL2/SDL.h>

#include <cstdint>

namespace platform {

inline int64_t GodotKeyFromSdlKeycode(SDL_Keycode k) {
    namespace key = godot::key;
    switch (k) {
    case SDLK_ESCAPE:    return key::ESCAPE;
    case SDLK_TAB:       return key::SPECIAL | 0x02;
    case SDLK_BACKSPACE: return key::BACKSPACE;
    case SDLK_RETURN:    return key::ENTER;
    case SDLK_KP_ENTER:  return key::KP_ENTER;
    case SDLK_LEFT:      return key::LEFT;
    case SDLK_UP:        return key::UP;
    case SDLK_RIGHT:     return key::RIGHT;
    case SDLK_DOWN:      return key::DOWN;
    default: break;
    }
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);
    if (k >= 32 && k < 127) return static_cast<int64_t>(k);   // digits, space, punctuation: ASCII
    return 0;
}

inline int64_t GodotKeyFromSdlScancode(SDL_Scancode s) {
    namespace key = godot::key;
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    switch (s) {
    case SDL_SCANCODE_0:         return '0';
    case SDL_SCANCODE_PERIOD:    return '.';
    case SDL_SCANCODE_COMMA:     return ',';
    case SDL_SCANCODE_SPACE:     return ' ';
    case SDL_SCANCODE_ESCAPE:    return key::ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return key::BACKSPACE;
    case SDL_SCANCODE_RETURN:    return key::ENTER;
    case SDL_SCANCODE_KP_ENTER:  return key::KP_ENTER;
    case SDL_SCANCODE_LEFT:      return key::LEFT;
    case SDL_SCANCODE_UP:        return key::UP;
    case SDL_SCANCODE_RIGHT:     return key::RIGHT;
    case SDL_SCANCODE_DOWN:      return key::DOWN;
    default:                     return 0;
    }
}

}  // namespace platform
