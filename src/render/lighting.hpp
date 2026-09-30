#pragma once
#include "core/math.hpp"

#include <glad/gl.h>

// Time-of-day lighting shared by every world shader through one uniform block
// (std140, binding 0, see shaders/frame.glsl). Colours are linear; the scene is
// rendered in HDR and tone mapped by PostFx.
struct Lighting {
    float sun[3]    = {0, 0, 0}; // direct sunlight
    float sky[3]    = {0, 0, 0}; // ambient skylight
    float haze[3]   = {0, 0, 0}; // air colour: background, distance fade
    Vec2  toSun     = {0, 0};    // horizontal unit direction towards the sun (x east, y north)
    Vec2  shadow    = {0, 0};    // ground offset of a shadow per metre of height
    float elevation = 0.0f;      // sine of the sun's elevation, negative below the horizon
    float night     = 0.0f;      // 0 day .. 1 night
    float golden    = 0.0f;      // 0 .. 1 around sunrise and sunset
    float lamps     = 0.0f;      // street lamps and lit windows, 0 off .. 1 on
    float exposure  = 1.0f;
};

// minutes: clock time, 0 .. 1440.
Lighting lightingAt(float minutes);

class FrameUniforms {
public:
    void init();
    // Uploads the block and binds it to uniform binding 0.
    void update(const Lighting& l, float dayMinutes, float realSeconds, float ppm, float renderScale);
    void destroy();

private:
    GLuint ubo_ = 0;
};
