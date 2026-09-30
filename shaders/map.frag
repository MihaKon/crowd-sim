#version 460
#include "frame.glsl"
// Ground surfaces: countryside fields, block interiors by type, water and shore.
// All procedural from the world position; fine detail fades out with zoom.

layout(location = 3) uniform float uSize;  // map side, metres
layout(location = 4) uniform vec2  uField; // shore distance field: origin, 1 / extent (metres)
layout(binding = 3) uniform sampler2D uShore;

float shoreDist(vec2 p) { return texture(uShore, (p - uField.x) * uField.y).r; } // < 0 in the water

in vec2       vWorld;
in float      vParam;
in float      vParam2;
flat in uint  vMaterial;
flat in uint  vColor;
out vec4      fragColor;

const uint kMatFlat = 0u, kMatLand = 1u, kMatBlock = 2u, kMatWater = 3u, kMatShore = 4u, kMatRail = 5u,
           kMatPlatform = 6u, kMatStation = 7u; // map_render.cpp

vec3 rgbOf(uint c) { // packed as in map_render.cpp: red in the low byte
    return toLinear(vec3(float(c & 255u), float((c >> 8u) & 255u), float((c >> 16u) & 255u)) / 255.0);
}
mat2 rot(float a) { return mat2(cos(a), sin(a), -sin(a), cos(a)); }

// Patchwork farmland: rotated plots with hedgerows, crops in rows.
vec3 countryside(vec2 p) {
    vec2  q    = rot(0.35) * p;
    vec2  size = vec2(190.0, 120.0);
    q.x       += size.x * 0.5 * step(0.5, fract(floor(q.y / size.y) * 0.5)); // offset every other row
    vec2  cell = floor(q / size);
    vec2  f    = q - cell * size;
    float h    = hash12(cell);
    vec3  c    = h < 0.30 ? hexLin(0xa7c17fu)   // meadow
               : h < 0.52 ? hexLin(0x92b46cu)   // green crop
               : h < 0.70 ? hexLin(0xd3c68au)   // wheat
               : h < 0.84 ? hexLin(0xb99f7cu)   // ploughed
                          : hexLin(0x86a86au);  // pasture
    // Plots smaller than a few pixels would alias: blend towards the average colour.
    c = mix(hexLin(0xa6b87eu), c, detailAt(50.0));
    c *= 0.92 + 0.16 * fbm3(p / 260.0);
    if (fTime.w < 0.4) return c; // rows and hedgerows are invisible from here
    float rows = detailAt(3.0) * (h > 0.30 && h < 0.84 ? 1.0 : 0.0);
    c *= 1.0 - 0.07 * rows * smoothstep(0.3, 0.7, abs(fract(f.y / 3.0) - 0.5) * 2.0);
    float edge = min(min(f.x, size.x - f.x), min(f.y, size.y - f.y));
    c = mix(c, hexLin(0x6f9150u), (1.0 - smoothstep(1.5, 3.5, edge)) * detailAt(2.0) * 0.85); // hedgerows
    return c;
}

vec3 blockGround(uint type, vec2 p, float edgePx) {
    // Residential, Commercial, Office, Industrial, Park, ResidentialPoor, ResidentialRich
    vec3  base;
    float green = 0.0; // share of gardens / lawn
    if (type == 0u)      { base = hexLin(0xdcd4c6u); green = 0.35; }
    else if (type == 1u) { base = hexLin(0xe2dcd2u); }
    else if (type == 2u) { base = hexLin(0xd6d9ddu); green = 0.12; }
    else if (type == 3u) { base = hexLin(0xc4bfb6u); }
    else if (type == 4u) { base = hexLin(0x9cc47au); green = 1.0; }
    else if (type == 5u) { base = hexLin(0xd2c8b8u); green = 0.12; }
    else                 { base = hexLin(0xa9c887u); green = 0.9; }
    vec3 c = base * (0.95 + 0.1 * fbm3(p / 45.0));
    if (fTime.w < 0.35) { // zoomed out: gardens as an average tint, no fine detail (and far cheaper)
        if (green > 0.0 && green < 1.0) c = mix(c, hexLin(0xa3c47eu), green * 0.55);
        return c;
    }

    if (green > 0.0 && green < 1.0) { // gardens in patches
        float g = fbm3(p / 22.0 + 7.0);
        vec3 lawn = hexLin(0xa3c47eu) * (0.9 + 0.2 * vnoise(p / 6.0));
        c = mix(c, lawn, smoothstep(1.0 - green - 0.04, 1.0 - green + 0.04, g));
    } else if (green >= 1.0) { // lawn with mowing stripes close up
        c *= 1.0 + 0.05 * detailAt(4.0) * (step(0.5, fract(dot(p, vec2(0.7, 0.7)) / 8.0)) - 0.5);
    }
    if (type == 3u) { // yards: darker asphalt lots
        vec2 cell = floor(p / 38.0);
        c = mix(c, hexLin(0x9a9790u), step(0.6, hash12(cell)) * 0.8);
    }
    if (type == 1u || type == 2u) { // paving joints close up
        vec2 j = abs(fract(p / 3.0) - 0.5);
        c *= 1.0 - 0.05 * detailAt(1.5) * step(0.46, max(j.x, j.y));
    }
    // Curb: a light kerb line and a soft shadow just inside it.
    float edgeM = edgePx / fTime.w;
    c = mix(c, hexLin(0xeeebe6u), (1.0 - smoothstep(0.2, 0.5, edgeM)) * detailAt(0.6));
    c *= mix(0.88, 1.0, smoothstep(0.4, 2.2, edgeM));
    return c;
}

vec3 ripples(vec2 p) {
    float t = fTime.y;
    return vec3(fbm3(p / 34.0 + vec2(t * 0.05, t * 0.03)) - fbm3b(p / 21.0 - vec2(t * 0.04, -t * 0.06)), 0.0, 0.0);
}

vec3 water(vec2 p) {
    float depth = -shoreDist(p);
    vec3  deep  = hexLin(0x2a6b9bu), mid = hexLin(0x3f93b5u), shallow = hexLin(0x6cc3c9u);
    vec3  c     = mix(shallow, mid, smoothstep(10.0, 90.0, depth));
    c           = mix(c, deep, smoothstep(80.0, 420.0, depth));
    c          *= 0.94 + 0.12 * fbm3(p / 900.0);
    if (fTime.w < 0.4) return lightFlat(c); // ripples and glints are invisible from here
    c          *= 1.0 + 0.10 * ripples(p).x;
    vec3  lit = lightFlat(c);
    float t   = fTime.y;
    // Sun glints: small sparkles where the ripples catch the sun.
    float g = vnoise(p / 3.0 + vec2(t * 0.5, t * 0.3)) * vnoise(p / 5.0 - vec2(t * 0.3, 0.0));
    lit += fSun.rgb * max(fSun.a, 0.0) * smoothstep(0.7, 0.9, g) * 0.8 * detailAt(1.5);
    return lit;
}

// Anti-aliased 1 inside [lo, hi].
float inside(float x, float lo, float hi) {
    float w = 0.7 * fwidth(x);
    return smoothstep(lo - w, lo + w, x) * (1.0 - smoothstep(hi - w, hi + w, x));
}

// Track bed: ballast, two tracks of sleepers and rails; far away a transit-map line
// in the line colour with a white casing.
vec4 rail(float a, float along, vec3 line) {
    float x    = a * 4.6; // metres across
    float grit = texture(uNoise, vWorld / 0.9).a * detailAt(0.2);
    vec3  bed  = hexLin(0x9a948bu) * (0.85 + 0.3 * grit + 0.1 * fbm3(vWorld / 3.0));
    bed       *= mix(0.8, 1.0, smoothstep(4.6, 3.6, abs(x)));
    for (int t = 0; t < 2; ++t) {
        float tc = (t == 0 ? -0.5 : 0.5) * 4.4; // kTrackGap
        float sl = inside(abs(x - tc), -1.0, 1.25) * inside(fract(along / 0.62), 0.0, 0.34) * detailAt(0.25);
        bed = mix(bed, hexLin(0xcbc6beu), sl);
        float rl = inside(abs(abs(x - tc) - 0.72), -1.0, 0.07) * detailAt(0.08);
        bed = mix(bed, hexLin(0xb9bdc4u), rl);
    }
    vec3  near  = lightFlat(bed);
    vec3  map   = lightFlat(mix(line, vec3(0.95), step(0.62, abs(a))));
    float close = smoothstep(1.0, 2.6, fTime.w);
    return vec4(mix(map, near, close), 1.0);
}

// Platform canopy: light roof, ribs, a stripe in the line colour on the outer edge.
vec4 platform(vec2 l, vec3 line) {
    vec3 c = hexLin(0xe6e8ebu) * (0.96 + 0.05 * fbm3(vWorld / 4.0));
    c *= 1.0 - 0.08 * inside(fract(l.x * 100.0 / 4.0), 0.0, 0.08) * detailAt(0.3); // ribs every 4 m
    c = mix(c, line, inside(l.y, 0.72, 1.01));
    vec3 lit = lightFlat(c) + vec3(1.0, 0.85, 0.65) * 1.2 * lampsF() * inside(l.y, -1.01, -0.8);
    return vec4(lit, smoothstep(0.6, 1.4, fTime.w)); // too thin to show far away
}

// Station: a paved square close up; far away a white map symbol with a dark ring,
// thicker for interchanges.
vec4 station(vec2 l, vec3 line, float lines) {
    float r    = length(l);
    float px   = 64.0 * fTime.w;            // kPlazaSize on screen
    float icon = 1.0 - smoothstep(18.0, 44.0, px);
    // Map symbol.
    float ring = lines > 1.5 ? 0.30 : 0.22;
    vec3  sym  = mix(vec3(0.95), hexLin(0x2f3440u), inside(r, 0.78 - ring, 0.80));
    float symA = 1.0 - smoothstep(0.78, 0.82, r);
    // Plaza: rounded square of paving, lit at night.
    vec2  q     = abs(l) - vec2(0.72);
    float box   = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 0.25; // rounded-square distance
    vec3  pave  = hexLin(0xdcd8d0u) * (0.95 + 0.06 * fbm3(vWorld / 2.0));
    pave *= 1.0 - 0.06 * inside(fract(r * 6.0), 0.0, 0.1) * detailAt(0.3);    // concentric paving
    pave = mix(pave, line, inside(box, -0.05, 0.0) * 0.9);                     // edge in the line colour
    vec3  plaza = lightFlat(pave) + pave * vec3(1.0, 0.72, 0.45) * 0.8 * lampsF() * (1.0 - r * 0.6);
    float plazaA = 1.0 - smoothstep(-0.01, 0.01, box);
    vec3  c = mix(plaza, lightFlat(sym) + sym * 0.4 * lampsF(), icon);
    return vec4(c, mix(plazaA, symA, icon));
}

void main() {
    vec2  p = vWorld;
    vec3  c;
    float alpha = 1.0;
    // Beyond the map the world dissolves into haze; far enough out, nothing else is left.
    float outside = max(max(-p.x, p.x - uSize), max(-p.y, p.y - uSize));
    float haze    = smoothstep(0.0, 0.18 * uSize, outside);
    if (haze >= 1.0) {
        fragColor = vec4(fHaze.rgb, 1.0);
        return;
    }

    if (vMaterial == kMatLand) {
        c = lightFlat(countryside(p));
    } else if (vMaterial == kMatBlock) {
        float edgePx = (1.0 - vParam) / max(fwidth(vParam), 1e-6);
        c = lightFlat(blockGround(vColor & 255u, p, edgePx));
    } else if (vMaterial == kMatWater) {
        c = water(p);
    } else if (vMaterial == kMatShore) {
        float a = vParam; // -1 open water .. 0 waterline .. 1 land
        float t = fTime.y;
        if (a < 0.0) { // foam rolling in at the waterline
            float s    = 1.0 + a; // 0 out at sea .. 1 at the waterline
            float band = sin(s * 9.0 - t * 1.6 + 4.0 * vnoise(p / 30.0)) * 0.5 + 0.5;
            float foam = smoothstep(0.2, 1.0, s) * (0.4 + 0.6 * band) *
                         smoothstep(0.35, 0.75, vnoise(p / 4.0 + t * 0.4));
            c     = lightFlat(vec3(0.93, 0.96, 0.97));
            alpha = max(foam, smoothstep(0.85, 1.0, s) * 0.8);
        } else { // wet sand, then a quay or beach
            c     = lightFlat(mix(hexLin(0xdcd2b4u), hexLin(0xc7c2b9u), step(0.5, vnoise(p / 300.0))));
            c    *= mix(0.82, 1.0, smoothstep(0.0, 0.25, a));
            alpha = 1.0 - smoothstep(0.6, 1.0, a);
        }
    } else if (vMaterial == kMatRail) {
        vec4 r = rail(vParam, vParam2, rgbOf(vColor));
        c = r.rgb;
    } else if (vMaterial == kMatPlatform) {
        vec4 r = platform(vec2(vParam, vParam2), rgbOf(vColor));
        c = r.rgb;
        alpha = r.a;
    } else if (vMaterial == kMatStation) {
        vec4 r = station(vec2(vParam, vParam2), rgbOf(vColor), float(vColor >> 24u));
        c = r.rgb;
        alpha = r.a;
    } else {
        c = lightFlat(rgbOf(vColor));
    }

    c = mix(c, fHaze.rgb, haze);
    fragColor = vec4(c, alpha);
}
