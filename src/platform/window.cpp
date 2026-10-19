#include "platform/window.h"

#include "godot/input.h"

#include "core/json_lite.h"
#include "platform/sdl_keys.h"
#include "platform/viewport_map.h"

#include <memory>
#include <string>

#include <cstdio>

namespace {

// The cursor images are small (32x32 RGBA); a whole-file read is simplest and
// happens only when CursorManager swaps the image.
bool ReadWholeFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    std::fclose(f);
    return true;
}

}  // namespace

namespace platform {

Window::~Window() { Close(); }

bool Window::Open(const char* title, int width, int height, bool vsync) {
    if (open_) return true;
    if (!GlCompatInit(&window_, &ctx_, title, width, height, vsync)) {
        // GlCompatInit already printed the specific failure; the SDL/GL reason
        // is the useful part, so no generic message replaces it.
        return false;
    }
    width_ = width;
    height_ = height;
    open_ = true;

    // The gamepad is not fatal if it is missing: desktop runs fine on the
    // keyboard, and failing to open a window because no pad is plugged in
    // would be absurd. On the Vita it is the only input there is.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        std::printf("input: no gamepad subsystem (%s); keyboard only\n",
                    SDL_GetError());
        return true;
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        pad_ = SDL_GameControllerOpen(i);
        if (pad_) {
            std::printf("input: gamepad '%s'\n",
                        SDL_GameControllerName(
                            static_cast<SDL_GameController*>(pad_)));
            break;
        }
    }
    return true;
}

void Window::Close() {
    if (!open_) return;
    if (pad_) {
        SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
        pad_ = nullptr;
    }
    if (cursor_) {
        SDL_FreeCursor(static_cast<SDL_Cursor*>(cursor_));
        cursor_ = nullptr;
        cursorImage_.clear();
    }
    GlCompatQuit(window_, ctx_);
    window_ = nullptr;
    ctx_ = nullptr;
    open_ = false;
}

void Window::ShowCursor(bool visible) {
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

// The image is the game's own, decoded to raw RGBA by
// tools/godot/export_cursors.gd: assets/ links no image decoder, and a cursor
// is no reason to put one in the shipping binary.
bool Window::SetCursor(const std::string& dir, const std::string& image, float hotX, float hotY) {
    if (image.empty() || image == cursorImage_) return true;

    std::string index;
    if (!ReadWholeFile(dir + "/cursors.json", index)) return false;
    const char* begin = index.data();
    const char* end = begin + index.size();
    const char* cb = nullptr;
    const char* ce = nullptr;
    if (!core::json::FindObject(begin, end, "cursors", &cb, &ce)) return false;
    const char* ib = nullptr;
    const char* ie = nullptr;
    if (!core::json::FindObject(cb, ce, image.c_str(), &ib, &ie)) return false;
    const int width = core::json::ReadIntTopLevel(ib, ie, "width", 0);
    const int height = core::json::ReadIntTopLevel(ib, ie, "height", 0);
    const std::string raw = core::json::ReadStringTopLevelDecoded(ib, ie, "raw");
    if (width <= 0 || height <= 0 || raw.empty()) return false;

    std::string pixels;
    if (!ReadWholeFile(dir + "/" + raw, pixels)) return false;
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (pixels.size() < expected) return false;

    // RGBA8, top row first, as the exporter writes it.
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        pixels.data(), width, height, 32, width * 4, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0xff000000u);
    if (!surface) return false;
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, static_cast<int>(hotX), static_cast<int>(hotY));
    SDL_FreeSurface(surface);
    if (!cursor) return false;

    SDL_SetCursor(cursor);
    if (cursor_) SDL_FreeCursor(static_cast<SDL_Cursor*>(cursor_));
    cursor_ = cursor;
    cursorImage_ = image;
    return true;
}

namespace {

// input_enums.h MouseButton: LEFT 1, RIGHT 2, MIDDLE 3. 0: one the game has no
// binding for (the extra buttons), which is dropped.
int MouseButtonFromSdl(uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:   return godot::mouse::LEFT;
    case SDL_BUTTON_RIGHT:  return godot::mouse::RIGHT;
    case SDL_BUTTON_MIDDLE: return godot::mouse::MIDDLE;
    default:                return 0;
    }
}

int MouseMaskFromSdl(uint32_t state) {
    int mask = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT))   mask |= godot::mouse::MASK_LEFT;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))  mask |= 1 << (godot::mouse::RIGHT - 1);
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) mask |= 1 << (godot::mouse::MIDDLE - 1);
    return mask;
}

}  // namespace

FrameInput Window::Poll() {
    FrameInput in{};
    // Screen pixels -> the 960x540 viewport the game is laid out in, the same
    // fit the composite is drawn with (platform/viewport_map.h).
    const ViewportMap map = FitKeepAspect(static_cast<float>(width_), static_cast<float>(height_));
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            in.quit = true;
            break;

        // Godot's InputEventMouseMotion: position and global_position are the
        // same in a viewport with no canvas transform, and `relative` is the
        // move in viewport units.
        case SDL_MOUSEMOTION: {
            auto m = std::make_shared<godot::InputEventMouseMotion>();
            m->position = ToViewport(map, static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
            m->global_position = m->position;
            m->relative = {static_cast<float>(e.motion.xrel) / map.scale,
                           static_cast<float>(e.motion.yrel) / map.scale};
            m->button_mask = MouseMaskFromSdl(e.motion.state);
            // As display_server_windows.cpp stamps it, before parsing.
            m->velocity = godot::Input::get_singleton().get_last_mouse_velocity();
            m->device = 0;
            in.events.push_back(m);
            break;
        }

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            auto b = std::make_shared<godot::InputEventMouseButton>();
            b->button_index = MouseButtonFromSdl(e.button.button);
            if (b->button_index == 0) break;
            b->pressed = e.type == SDL_MOUSEBUTTONDOWN;
            b->double_click = e.button.clicks == 2;
            b->position = ToViewport(map, static_cast<float>(e.button.x), static_cast<float>(e.button.y));
            b->global_position = b->position;
            b->button_mask = MouseMaskFromSdl(SDL_GetMouseState(nullptr, nullptr));
            b->device = 0;
            in.events.push_back(b);
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool down = e.type == SDL_KEYDOWN;
            // Debug overlay keys never reach the game (F1 / [ / ], matching the
            // Vita's SELECT / L / R).
            if (e.key.keysym.sym == SDLK_F1 || e.key.keysym.sym == SDLK_LEFTBRACKET ||
                e.key.keysym.sym == SDLK_RIGHTBRACKET) {
                if (down && !e.key.repeat) {
                    if (e.key.keysym.sym == SDLK_F1) in.toggleDebug = true;
                    if (e.key.keysym.sym == SDLK_LEFTBRACKET) in.debugPrev = true;
                    if (e.key.keysym.sym == SDLK_RIGHTBRACKET) in.debugNext = true;
                }
                break;
            }
            auto k = std::make_shared<godot::InputEventKey>();
            k->keycode = GodotKeyFromSdlKeycode(e.key.keysym.sym);
            k->physical_keycode = GodotKeyFromSdlScancode(e.key.keysym.scancode);
            k->pressed = down;
            k->echo = e.key.repeat != 0;   // Godot delivers key repeats as echo events
            k->device = 0;                 // Godot's keyboard device; ui_* nav is pinned to 0
            in.events.push_back(k);
            if (down && !e.key.repeat) {
                if (e.key.keysym.sym == SDLK_UP)     in.navUp = true;
                if (e.key.keysym.sym == SDLK_DOWN)   in.navDown = true;
                if (e.key.keysym.sym == SDLK_LEFT)   in.navLeft = true;
                if (e.key.keysym.sym == SDLK_RIGHT)  in.navRight = true;
                if (e.key.keysym.sym == SDLK_RETURN) in.confirm = true;
            }
            break;
        }

        // SDL_GameControllerButton A..DPAD_RIGHT is 0..14, the same order as
        // Godot's JoyButton (input_enums.h:64-78), so the index passes through.
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            const bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
            const int b = e.cbutton.button;
            if (b == SDL_CONTROLLER_BUTTON_BACK || b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
                b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                if (down) {
                    if (b == SDL_CONTROLLER_BUTTON_BACK) in.toggleDebug = true;
                    if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) in.debugPrev = true;
                    if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) in.debugNext = true;
                }
                break;
            }
            auto j = std::make_shared<godot::InputEventJoypadButton>();
            j->button_index = b;
            j->pressed = down;
            // Godot numbers joypads from 0 in connection order; the first opened
            // pad is device 0, which is the one ui_* navigation is pinned to.
            j->device = SDL_GameControllerFromInstanceID(e.cbutton.which) ==
                                static_cast<SDL_GameController*>(pad_) ? 0 : 1;
            in.events.push_back(j);
            if (down) {
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP)    in.navUp = true;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN)  in.navDown = true;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT)  in.navLeft = true;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) in.navRight = true;
                if (b == SDL_CONTROLLER_BUTTON_A)          in.confirm = true;
            }
            break;
        }

        // Hot-plug. On the Vita the pad is present at boot, but on desktop a
        // controller attached after launch would otherwise never be seen.
        case SDL_CONTROLLERDEVICEADDED:
            if (!pad_) pad_ = SDL_GameControllerOpen(e.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (pad_ && SDL_GameControllerFromInstanceID(e.cdevice.which) == pad_) {
                SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
                pad_ = nullptr;
            }
            break;
        default:
            break;
        }
    }
    return in;
}

void Window::Swap() {
    if (open_) GlCompatSwap(window_);
}

std::string ExeFolder() {
    char* base = SDL_GetBasePath();
    if (!base) return ".";
    std::string dir = base;
    SDL_free(base);
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    return dir;
}

}  // namespace platform
