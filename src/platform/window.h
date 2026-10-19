// The window, the GL context and the input pump.
//
// The only place SDL lifetime is managed. core/, seq/ and assets/ never see
// it, which keeps the simulation unit-testable headless and portable to
// arm-vita-eabi unchanged.
#pragma once

#include "platform/gl_compat.h"
#include "platform/input.h"

#include <string>

namespace platform {

class Window {
public:
    Window() = default;
    ~Window();

    // Non-copyable: it owns a GL context and an SDL window.
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Open(const char* title, int width, int height, bool vsync = true);
    void Close();

    // Pumps SDL events and returns this frame's intent.
    FrameInput Poll();

    void Swap();

    // CursorManager decides both: Input.set_mouse_mode hides the pointer while
    // the controller drives, and set_custom_mouse_cursor swaps the image as it
    // moves over the table. `image` is the resource's file name
    // ("cursor xp_hover.png"); `dir` holds the extracted cursors. False, with a
    // reason logged by the caller, leaves the arrow in place.
    void ShowCursor(bool visible);
    bool SetCursor(const std::string& dir, const std::string& image, float hotX, float hotY);

    int Width()  const { return width_; }
    int Height() const { return height_; }
    bool IsOpen() const { return open_; }

private:
    SDL_Window*   window_ = nullptr;
    SDL_GLContext ctx_    = nullptr;

    // The gamepad, as void* so this header does not drag SDL's controller API
    // into everything that needs a window. On the Vita this is always present;
    // on desktop it may be null, and every use is guarded.
    void* pad_ = nullptr;
    // The SDL cursor built from the game's own image, as void* for the same
    // reason as the pad.
    void* cursor_ = nullptr;
    std::string cursorImage_;
    int  width_  = 0;
    int  height_ = 0;
    bool open_   = false;
};

// The running exe's folder in UTF-8, without a trailing slash; "." when SDL
// cannot tell.
std::string ExeFolder();

}  // namespace platform
