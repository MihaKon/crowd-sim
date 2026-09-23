#pragma once
#include <cmath>

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

inline Vec2  operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2  operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2  operator-(Vec2 a) { return {-a.x, -a.y}; }
inline Vec2  operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2  operator*(float s, Vec2 a) { return a * s; }
inline Vec2  operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float length2(Vec2 a) { return dot(a, a); }
inline float length(Vec2 a) { return std::sqrt(length2(a)); }
inline Vec2  normalize(Vec2 a) {
    const float l = length(a);
    return l > 0.0f ? a / l : Vec2{};
}
inline Vec2 perp(Vec2 a) { return {-a.y, a.x}; }
inline Vec2 rotate(Vec2 a, float r) {
    const float c = std::cos(r), s = std::sin(r);
    return {a.x * c - a.y * s, a.x * s + a.y * c};
}
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2  lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
