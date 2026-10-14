/*
  Meridian, a UCI chess engine
  Copyright (C) 2026 Syed Taha

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
