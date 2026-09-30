#include "render/lighting.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi      = 3.14159265f;
constexpr float kSunrise = 5.0f * 60.0f + 15.0f;  // 05:15
constexpr float kSunset  = 18.0f * 60.0f + 45.0f; // 18:45
constexpr float kMaxElev = 62.0f * kPi / 180.0f;  // at noon

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Rgb {
    float r, g, b;
};
Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb scale(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }
void store(float* dst, Rgb c) {
    dst[0] = c.r;
    dst[1] = c.g;
    dst[2] = c.b;
}

} // namespace

Lighting lightingAt(float minutes) {
    Lighting l;
    // Day: the sun goes from the east through the south to the west. Night: the same
    // sine continues below the horizon, so dusk and dawn are symmetric.
    const float dayLen = kSunset - kSunrise;
    float       phase  = (minutes - kSunrise) / dayLen; // 0 sunrise .. 1 sunset
    if (phase < -0.5f) phase += 1440.0f / dayLen;
    const float elev    = kMaxElev * std::sin(kPi * std::clamp(phase, -0.5f, 1.5f));
    const float az      = kPi * 0.5f + kPi * std::clamp(phase, 0.0f, 1.0f); // from north, clockwise
    l.elevation         = std::sin(elev);
    l.toSun             = {std::sin(az), std::cos(az)};
    const float lenPerM = std::min(1.0f / std::tan(std::max(elev, 0.05f)), 3.2f);
    l.shadow            = l.toSun * -lenPerM;

    const float deg = elev * 180.0f / kPi;
    l.night         = 1.0f - smooth(-7.0f, 3.0f, deg);
    l.golden        = smooth(-4.0f, 2.0f, deg) * (1.0f - smooth(6.0f, 20.0f, deg));
    l.lamps         = 1.0f - smooth(-2.0f, 6.0f, deg);

    // Sunlight: white at noon, orange low in the sky, gone below the horizon.
    const Rgb noonSun{1.00f, 0.95f, 0.86f}, lowSun{1.00f, 0.58f, 0.30f};
    const float up = smooth(-1.0f, 8.0f, deg);
    store(l.sun, scale(mix(lowSun, noonSun, smooth(4.0f, 30.0f, deg)), 2.6f * up));

    // Skylight: blue by day, violet at dusk, deep blue at night.
    const Rgb daySky{0.52f, 0.62f, 0.80f}, duskSky{0.50f, 0.43f, 0.50f}, nightSky{0.030f, 0.034f, 0.050f};
    Rgb sky = mix(duskSky, daySky, smooth(2.0f, 25.0f, deg));
    sky     = mix(sky, nightSky, l.night);
    store(l.sky, scale(sky, 0.85f));

    const Rgb dayHaze{0.56f, 0.64f, 0.78f}, duskHaze{0.55f, 0.42f, 0.42f}, nightHaze{0.008f, 0.010f, 0.018f};
    Rgb haze = mix(duskHaze, dayHaze, smooth(2.0f, 25.0f, deg));
    store(l.haze, mix(haze, nightHaze, l.night));

    // The eye adapts: nights are brightened so the city stays readable.
    l.exposure = 0.40f + 1.1f * l.night;
    return l;
}

namespace {

// std140 layout, must match shaders/frame.glsl.
struct Block {
    float sun[4];    // rgb, a: sine of elevation
    float sky[4];    // rgb, a: night
    float sunDir[4]; // xy: towards the sun, zw: shadow per metre of height
    float haze[4];   // rgb, a: golden hour
    float time[4];   // x: minutes of the day, y: real seconds, z: lamps, w: ppm
    float view[4];   // x: exposure, y: render scale (scene pixels per window pixel)
};

} // namespace

void FrameUniforms::init() {
    glCreateBuffers(1, &ubo_);
    glNamedBufferStorage(ubo_, sizeof(Block), nullptr, GL_DYNAMIC_STORAGE_BIT);
}

void FrameUniforms::update(const Lighting& l, float dayMinutes, float realSeconds, float ppm, float renderScale) {
    const Block b{{l.sun[0], l.sun[1], l.sun[2], l.elevation},
                  {l.sky[0], l.sky[1], l.sky[2], l.night},
                  {l.toSun.x, l.toSun.y, l.shadow.x, l.shadow.y},
                  {l.haze[0], l.haze[1], l.haze[2], l.golden},
                  {dayMinutes, realSeconds, l.lamps, ppm},
                  {l.exposure, renderScale, 0.0f, 0.0f}};
    glNamedBufferSubData(ubo_, 0, sizeof(Block), &b);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_);
}

void FrameUniforms::destroy() {
    glDeleteBuffers(1, &ubo_);
    ubo_ = 0;
}
