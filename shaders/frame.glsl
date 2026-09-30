// Per-frame lighting and shared helpers for the world shaders. The scene is
// rendered in linear HDR; PostFx tone maps it. Must match lighting.cpp (std140).
layout(std140, binding = 0) uniform Frame {
    vec4 fSun;    // rgb: direct sunlight, a: sine of the sun's elevation (< 0 below the horizon)
    vec4 fSky;    // rgb: ambient skylight, a: night 0..1
    vec4 fSunDir; // xy: horizontal direction towards the sun, zw: shadow offset per metre of height
    vec4 fHaze;   // rgb: air colour, a: golden hour 0..1
    vec4 fTime;   // x: minutes of the day, y: real seconds, z: lamps 0..1, w: pixels per metre
    vec4 fView;   // x: exposure, y: render scale (scene pixels per window pixel)
};

float nightF() { return fSky.a; }
float lampsF() { return fTime.z; }

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 hexLin(uint c) {
    return toLinear(vec3(float((c >> 16u) & 255u), float((c >> 8u) & 255u), float(c & 255u)) / 255.0);
}

vec3 sunVector() { // unit vector towards the sun (x east, y north, z up)
    float e = fSun.a;
    return vec3(fSunDir.xy * sqrt(max(1.0 - e * e, 0.0)), e);
}
// Any surface with unit normal n: sun (only above the horizon) plus sky, less from below.
vec3 lightN(vec3 albedo, vec3 n) {
    float d = max(dot(n, sunVector()), 0.0) * step(0.0, fSun.a);
    return albedo * (fSun.rgb * d + fSky.rgb * (0.6 + 0.4 * n.z));
}
// Flat ground or roof: sun from above plus sky.
vec3 lightFlat(vec3 albedo) { return albedo * (fSun.rgb * max(fSun.a, 0.0) + fSky.rgb); }
// Vertical wall with horizontal outward normal n.
vec3 lightWall(vec3 albedo, vec2 n) {
    float cosE = sqrt(max(1.0 - fSun.a * fSun.a, 0.0));
    float d    = max(dot(n, fSunDir.xy), 0.0) * cosE * step(0.0, fSun.a);
    return albedo * (fSun.rgb * d + fSky.rgb * 0.8);
}

// Overlays (data layers, markers) must look the same at any time of day: this
// returns the scene value that PostFx's exposure and ACES curve turn back into the
// given display colour (sRGB).
vec3 overlayColor(vec3 srgb) {
    vec3 y = min(toLinear(srgb), vec3(0.98));
    vec3 a = 2.43 * y - 2.51, b = 0.59 * y - 0.03, c = 0.14 * y;
    vec3 x = (-b - sqrt(max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a);
    return x / fView.x;
}
vec3 overlayHex(uint c) {
    return overlayColor(vec3(float((c >> 16u) & 255u), float((c >> 8u) & 255u), float(c & 255u)) / 255.0);
}

// ---- noise
float hash12(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
vec2 hash22(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy);
}
// Filtered noise from NoiseTexture (unit 7): one fetch, mipmapped, tiles every
// 32 (vnoise) or 16 (fbm) units.
layout(binding = 7) uniform sampler2D uNoise;
float vnoise(vec2 p) { return texture(uNoise, p / 32.0).r; } // value noise, lattice 1
float fbm3(vec2 p) { return texture(uNoise, p / 16.0).g; }   // 3 octaves, base lattice 1
float fbm3b(vec2 p) { return texture(uNoise, p / 16.0).b; }  // another seed
// Fades detail out once a feature of `metres` gets smaller than ~2 pixels.
float detailAt(float metres) { return smoothstep(1.0, 3.0, metres * fTime.w); }
