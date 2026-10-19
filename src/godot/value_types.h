// Godot's plain value types, in a leaf header so anything can include them
// without pulling in the node model (input_event.h is included by script.h,
// which node.h includes).
#pragma once

namespace godot {

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

}  // namespace godot
