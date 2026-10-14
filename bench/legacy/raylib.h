// Stand-in for the parts of raylib that the original engine's board code
// touches, so that code builds without a window, a GPU or an audio device.
// Every function does nothing. Used only to benchmark the original engine.
#pragma once

#include <cstdarg>

struct Color {
    unsigned char r, g, b, a;
};

struct Vector2 {
    float x, y;
};

struct Rectangle {
    float x, y, width, height;
};

struct Texture2D {
    unsigned int id;
    int width, height, mipmaps, format;
};

struct Sound {
    int unused;
};

struct Font {
    int unused;
};

struct Image {
    int unused;
};

constexpr int MOUSE_LEFT_BUTTON = 0;
constexpr Color WHITE = {255, 255, 255, 255};
constexpr Color RED = {230, 41, 55, 255};

inline Color Fade(Color color, float) {
    return color;
}

inline bool IsMouseButtonPressed(int) {
    return false;
}

inline bool IsMouseButtonReleased(int) {
    return false;
}

inline bool CheckCollisionPointRec(Vector2, Rectangle) {
    return false;
}

inline Vector2 GetMousePosition() {
    return {0, 0};
}

inline int GetMouseX() {
    return 0;
}

inline int GetMouseY() {
    return 0;
}

inline int GetScreenWidth() {
    return 0;
}

inline int GetScreenHeight() {
    return 0;
}

inline Texture2D LoadTexture(const char *) {
    return {0, 0, 0, 0, 0};
}

inline void UnloadTexture(Texture2D) {
}

inline void PlaySound(Sound) {
}
