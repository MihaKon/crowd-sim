#version 460
#include "frame.glsl"
// Procedural roofs and facades, lit by the sun and sky; at night a share of the
// windows (depending on the hour and the building) glows, for the bloom.

layout(location = 2) uniform float uPpm;
layout(location = 4) uniform float uFade; // fading in when zooming in, 0..1

in vec2       vLocal;
flat in uint  vKind;
flat in vec3  vNormal;
flat in vec4  vSize;   // sizeAlong, sizeIn, height, wall length
flat in uint  vInfo;
flat in float vPitch;
out vec4      fragColor;

uint hashU(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}
float rnd(uint x) { return float(hashU(x) & 0xFFFFu) / 65535.0; }

// Anti-aliased 1 inside [lo, hi].
float inside(float x, float lo, float hi) {
    float w = 0.7 * fwidth(x);
    return smoothstep(lo - w, lo + w, x) * (1.0 - smoothstep(hi - w, hi + w, x));
}

// Facades (buildings.cpp pickStyle): warm plaster, light plaster, concrete, glass,
// old walls, corrugated metal, white villa.
const uint kFacade[7] = uint[](0xe9dfd0u, 0xe2e0e6u, 0xc6cad1u, 0x7d95afu, 0xab9f94u, 0xb9bfc6u, 0xf5f2edu);
const uint kPastel[8] = uint[](0xefe3ccu, 0xefcfbau, 0xd3dfc5u, 0xcddbe9u, 0xdcd1e5u, 0xe6d3b1u, 0xf2efe8u, 0xe0b59du);

// Share of lit windows by the clock: homes in the evening, offices after work.
float litShare(bool office) {
    float h = fTime.x / 60.0;
    if (office) return h >= 7.0 && h < 18.5 ? 0.5 : h >= 18.5 && h < 23.5 ? mix(0.45, 0.05, (h - 18.5) / 5.0) : 0.03;
    if (h >= 16.5 && h < 23.0) return 0.62;
    if (h >= 23.0) return mix(0.62, 0.18, (h - 23.0));
    if (h < 5.0) return 0.08 + 0.1 * max(0.0, 1.0 - h);
    return h < 8.0 ? 0.3 : 0.12;
}

vec3 roof(uint style, uint seed) {
    vec2  p   = vLocal;
    vec2  sz  = vSize.xy;
    float edge = min(min(p.x, sz.x - p.x), min(p.y, sz.y - p.y));
    vec3  c;
    if (vPitch > 0.0 || style <= 1u) { // tiles
        uint  v   = seed % 4u;
        uint  pal = style == 0u ? (v == 0u ? 0xb8634bu : v == 1u ? 0x9e5344u : v == 2u ? 0x5e6570u : 0x8a5a48u)
                                : (v == 0u ? 0xc98b55u : v == 1u ? 0x8c6a55u : v == 2u ? 0x6f8195u : 0x8e8680u);
        c = hexLin(pal) * (0.93 + 0.12 * fbm3(p / 5.0 + float(seed % 97u)));
        if (vPitch > 0.0) {
            bool  along = sz.x >= sz.y;
            float d     = along ? abs(p.y - 0.5 * sz.y) : abs(p.x - 0.5 * sz.x); // from the ridge
            c *= 1.0 - 0.10 * detailAt(0.3) * (1.0 - inside(fract(d / 0.33), 0.12, 1.0)); // tile rows
            c = mix(c, c * 1.25, inside(d, -1.0, 0.18) * detailAt(0.3));                    // ridge cap
        }
        c *= mix(0.8, 1.0, smoothstep(0.0, 0.35, edge)); // eaves
        return c;
    }
    if (fTime.w < 1.0) { // zoomed out: plain roof colour and parapet shade only
        uint v    = seed % 5u;
        uint base = style == 5u ? 0x86ad62u : v == 0u ? 0x9ea2a9u : v == 1u ? 0xb8b5afu : style == 2u ? 0xd2d0ccu
                  : style == 3u ? 0xc7cacfu : 0xd8d2c8u;
        return hexLin(base) * mix(0.86, 1.0, smoothstep(0.4, 1.3, edge));
    }
    if (style == 5u) { // green roof
        c = hexLin(0x86ad62u) * (0.85 + 0.3 * fbm3(p / 2.5 + float(seed % 53u)));
    } else {
        // Flat roofs: mostly light, some a darker membrane.
        uint v    = seed % 5u;
        uint base = v == 0u    ? 0x9ea2a9u
                  : v == 1u    ? 0xb8b5afu
                  : style == 2u ? 0xd2d0ccu
                  : style == 3u ? 0xc7cacfu
                                : 0xd8d2c8u;
        c = hexLin(base) * (0.95 + 0.08 * fbm3(p / 3.0 + float(seed % 89u)));
        // Rooftop units on a 5 m grid, away from the parapet.
        vec2 cell = floor(p / 5.0), f = p - cell * 5.0;
        uint h    = hashU(seed ^ uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u);
        bool room = edge > 2.0 && p.x > 1.5 && p.y > 1.5 && p.x < sz.x - 1.5 && p.y < sz.y - 1.5;
        if (room && (h & 1023u) < 190u) {
            float unit   = inside(f.x, 1.2, 3.4) * inside(f.y, 1.4, 3.2);
            vec2  sh     = -fSunDir.xy * 0.8; // its shadow, towards the shadow side
            float shadow = inside(f.x - sh.x, 1.2, 3.4) * inside(f.y - sh.y, 1.4, 3.2) * (1.0 - unit);
            c = mix(c, hexLin(0xe9e8e4u), unit * detailAt(0.8));
            c *= 1.0 - 0.3 * shadow * detailAt(0.8) * step(0.0, fSun.a);
        }
        if (style == 3u) c = mix(c, c * 0.8, inside(edge, 0.5, 1.1) * detailAt(0.4)); // frame
    }
    // Parapet: a light rim with a little shade just inside.
    c = mix(c, hexLin(0xefede9u), inside(edge, -1.0, 0.4) * detailAt(0.25));
    c *= mix(0.86, 1.0, smoothstep(0.4, 1.3, edge));
    return c;
}

// Returns the lit wall colour (HDR, emission included).
vec3 wall(uint seed) {
    uint  facade  = min((vInfo >> 4u) & 7u, 6u);
    uint  pattern = (vInfo >> 7u) & 3u; // 0 houses, 1 office bands, 2 shop fronts, 3 industrial
    vec2  w       = vLocal;
    float len = vSize.w, height = vSize.z;
    // Plastered homes and shops get one of a range of pastel tints.
    uint  tint = kFacade[facade];
    if (facade <= 1u && pattern != 1u) tint = kPastel[(seed >> 3u) % 8u];
    vec3  c = hexLin(tint) * (0.96 + 0.06 * fbm3(w / 4.0 + float(seed % 61u)));
    bool  glassTower = facade == 3u;
    bool  office     = pattern == 1u;

    float winDetail = detailAt(1.2);
    float winArea   = glassTower ? 0.8 : office ? 0.55 : pattern == 3u ? 0.12 : 0.22; // share of the wall
    if (winDetail <= 0.0) { // windows too small to see: tint towards glass, glow with their average at night
        c = mix(c, hexLin(0x3b4a5eu), winArea * 0.5) * mix(0.78, 1.0, smoothstep(0.0, 1.6, w.y));
        return lightWall(c, vNormal.xy) +
               vec3(1.0, 0.72, 0.45) * 1.9 * winArea * litShare(office || glassTower) * lampsF() * step(3.0, height);
    }

    float fl  = floor(w.y / 3.0), fy = w.y - fl * 3.0; // floor index, metres into the floor
    float bay = office ? 1.6 : pattern == 3u ? 3.5 : 2.6;
    float bi  = floor(w.x / bay), bx = w.x - bi * bay;
    float win;
    if (glassTower)                     win = inside(fy, 0.25, 2.85) * inside(bx, 0.06, bay - 0.06);
    else if (office)                    win = inside(fy, 0.75, 2.6) * inside(bx, 0.12, bay - 0.12);
    else if (pattern == 3u)             win = inside(w.y, height - 2.2, height - 1.2) * inside(bx, 0.3, bay);
    else if (pattern == 2u && fl < 0.5) win = inside(fy, 0.3, 2.5) * inside(bx, 0.2, bay - 0.2);
    else                                win = inside(fy, 1.0, 2.25) * inside(bx, 0.75, 1.85);
    win *= inside(w.x, 0.5, len - 0.5) * step(w.y, height - 0.6); // not at the wall ends / parapet
    if (pattern == 3u && w.y < 4.0 && bx > 1.0 && bx < 2.5 && uint(bi) % 3u == 1u) c *= 0.62; // loading doors
    win *= winDetail;

    // Glass: dark, reflecting some sky, brighter towards the top of tall towers.
    vec3 glass = hexLin(0x3b4a5eu) + fSky.rgb * 0.18 +
                 vec3(0.02, 0.03, 0.05) * smoothstep(0.0, 1.0, w.y / max(height, 1.0));
    c = mix(c, glass, win);

    c *= mix(0.78, 1.0, smoothstep(0.0, 1.6, w.y));                              // ground contact
    c = mix(c, c * 1.12, inside(w.y, height - 0.35, height + 1.0) * detailAt(0.3)); // cornice
    vec3 lit = lightWall(c, vNormal.xy);

    // Night: lit windows glow warm, some cool, a few TV blue.
    if (win > 0.0 && lampsF() > 0.0) {
        uint  h  = hashU(seed ^ (uint(bi) * 747796405u) ^ (uint(fl) * 2891336453u));
        float on = step(rnd(h), litShare(office || glassTower));
        float k  = rnd(h >> 7u);
        vec3  e  = k < 0.7 ? vec3(1.0, 0.70, 0.40) : k < 0.92 ? vec3(0.85, 0.90, 1.0) : vec3(0.40, 0.55, 1.0) * 0.6;
        lit += e * (1.4 + 1.2 * rnd(h >> 3u)) * on * win * lampsF();
    }
    // Too far away to see single windows: the wall glows with their average.
    lit += vec3(1.0, 0.72, 0.45) * 1.9 * winArea * litShare(office || glassTower) * (1.0 - winDetail) * lampsF() *
           step(3.0, height);
    return lit;
}

void main() {
    uint seed  = vInfo >> 9u;
    uint style = vInfo & 7u;
    vec3 c;
    if (vKind == 1u) {
        c = lightN(roof(style, seed), vNormal);
        // Aircraft warning lights on the corners of tall towers, blinking.
        if (vSize.z > 60.0 && lampsF() > 0.0) {
            vec2  p = vLocal, sz = vSize.xy;
            float d = length(min(p, sz - p));
            float blink = step(fract(fTime.y * 0.6 + float(seed % 7u) * 0.13), 0.3);
            c += vec3(1.0, 0.08, 0.05) * 30.0 * (1.0 - smoothstep(0.3, 0.9, d)) * blink * lampsF();
        }
    } else {
        c = wall(seed);
    }
    fragColor = vec4(c, uFade);
}
