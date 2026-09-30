#version 460
#include "frame.glsl"
// Street surface from the pixel's position on the street. Pass 0: sidewalk over the
// whole quad; pass 1: carriageway with Japanese markings (drive on the left). Close
// up: asphalt, kerbs, markings (anti-aliased); zoomed out it becomes a clean map line.
// At night street lamps along both kerbs light warm pools.

layout(location = 2) uniform float uPpm;
layout(location = 3) uniform int   uPass;

in vec2       vLocal;
in vec2       vWorld;
flat in uvec4 vInfo;
flat in float vLength;
flat in float vWidth;
out vec4      fragColor;

const float kSidewalk = 2.5;  // must match roads.cpp
const float kLampGap  = 28.0; // metres between lamps on one side

// Anti-aliased band: 1 where lo < x < hi, soft over one pixel.
float band(float x, float lo, float hi) {
    float w = 0.75 / uPpm;
    return smoothstep(lo - w, lo + w, x) * (1.0 - smoothstep(hi - w, hi + w, x));
}

// Warm light from the lamps on both kerbs (staggered) and one over each junction,
// plus the lamp heads themselves (bright enough to bloom).
vec3 lampLight(float along, float across, float roadHalf, bool junction) {
    if (lampsF() <= 0.0) return vec3(0.0);
    vec3  warm = vec3(1.0, 0.66, 0.36);
    float d2;
    if (junction) {
        d2 = across * across;
    } else {
        float side = across < 0.0 ? 0.0 : 0.5;
        float k    = floor(along / kLampGap - side) + 0.5 + side;
        float dA   = along - k * kLampGap;
        float dX   = abs(across) - (roadHalf + 0.6);
        d2 = dA * dA + dX * dX;
    }
    vec3  pool = warm * 0.45 * exp(-d2 / 55.0);
    float head = exp(-d2 / 0.12) * 5.0 * detailAt(0.8);
    return (pool + warm * head) * lampsF();
}

void main() {
    uint  kind  = vInfo.x, flags = vInfo.y;
    float near  = smoothstep(1.2, 3.5, uPpm); // realistic surface close up, map style far away
    bool  junction = kind == 2u;

    float across, roadHalf;
    bool  art;
    if (junction) {
        float r = length(vLocal);
        if (r > 1.0) discard;
        across   = r * 0.5 * vWidth;
        roadHalf = vLength;
        art      = roadHalf > 4.0;
    } else {
        art      = kind == 1u;
        roadHalf = art ? 8.0 : 3.5;
        across   = vLocal.y * 0.5 * vWidth;
    }
    float along = vLocal.x;
    float av    = abs(across);
    vec3  lamp  = lampLight(along, across, roadHalf, junction) * mix(0.45, 1.0, near);

    // Map style: pale casing, white streets, soft yellow arterials.
    vec3 mapCasing = hexLin(art ? 0xd8c38eu : 0xc9c4bcu);
    vec3 mapFill   = hexLin(art ? 0xf6df9cu : 0xf7f5f1u);
    // The carriageway pass paints over the middle: the sidewalk pass skips it.
    if (uPass == 0 && av < roadHalf - 0.5) discard;
    if (near <= 0.0) { // zoomed out: only the map style (and far cheaper)
        if (uPass == 1 && av > roadHalf) discard;
        vec3 surf = uPass == 0 ? mapCasing : mapFill;
        fragColor = vec4(lightFlat(surf) + surf * lamp, 1.0);
        return;
    }

    if (uPass == 0) { // ---- sidewalk
        vec3 walk = hexLin(0xdedad3u) * (0.96 + 0.08 * vnoise(vWorld / 1.2));
        if (!junction) {
            float j = abs(fract(along / 1.5) - 0.5); // slab joints
            walk *= 1.0 - 0.06 * detailAt(0.4) * (1.0 - smoothstep(0.44, 0.47, 0.5 - j));
        }
        float kerb = band(av, roadHalf - 0.05, roadHalf + 0.25);
        walk = mix(walk, hexLin(0xf0eeea), kerb);
        vec3 surf = mix(mapCasing, walk, near);
        fragColor = vec4(lightFlat(surf) + surf * lamp, 1.0);
        return;
    }

    // ---- carriageway
    if (av > roadHalf) discard;
    vec3 asphalt = hexLin(art ? 0x5f636cu : 0x6a6e76u);
    asphalt *= 0.93 + 0.1 * fbm3(vWorld / 6.0) + 0.05 * (texture(uNoise, vWorld / 1.5).a - 0.5) * detailAt(0.3);
    asphalt *= mix(0.9, 1.0, smoothstep(0.0, 0.6, roadHalf - av)); // gutter
    vec3 c = asphalt;

    if (!junction) {
        float trimA = float(vInfo.z) * 0.25, trimB = float(vInfo.w) * 0.25;
        float dA = along - trimA, dB = vLength - along - trimB; // past the junction mouth
        float white = 0.0, yellow = 0.0;
        if (dA > 0.0 && dB > 0.0) {
            bool zebraA = (flags & 1u) != 0u && dA > 0.6 && dA < 3.8;
            bool zebraB = (flags & 2u) != 0u && dB > 0.6 && dB < 3.8;
            if (zebraA || zebraB) {
                white = band(fract(across / 1.0), 0.0, 0.5) * step(av, roadHalf - 0.3); // 50 cm bars
            } else if ((flags & 4u) != 0u && dA > 4.2 && dA < 4.65 && across < 0.0) {
                white = 1.0; // stop line, incoming lane at the start (traffic from B keeps left)
            } else if ((flags & 8u) != 0u && dB > 4.2 && dB < 4.65 && across > 0.0) {
                white = 1.0; // stop line, incoming lane at the end
            } else {
                float gA = (flags & 5u) != 0u ? 4.8 : 0.5, gB = (flags & 10u) != 0u ? 4.8 : 0.5;
                if (dA > gA && dB > gB) {
                    if (art) {
                        yellow = band(av, 0.15, 0.45);                                      // centre line
                        white  = band(av, 3.85, 4.15) * step(fract(along / 6.0), 0.5);     // lane divider
                        white  = max(white, band(av, 7.2, 7.5));                            // edge line
                    } else {
                        white = band(av, 2.9, 3.15); // side strip line
                    }
                }
            }
        }
        float paint = detailAt(0.35);
        c = mix(c, hexLin(0xf2f2eeu), white * 0.9 * paint);
        c = mix(c, hexLin(0xf0c24bu), yellow * 0.9 * paint);
    }
    vec3 surf = mix(mapFill, c, near);
    fragColor = vec4(lightFlat(surf) + surf * lamp, 1.0);
}
