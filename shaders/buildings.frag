#version 460
// Roofs repeat their atlas tile (pixel art is never stretched); walls get procedural
// windows on a 25 cm grid, a random share of which light up at night.

layout(binding = 0) uniform sampler2D uAtlas;
layout(location = 2) uniform float uPpm;
layout(location = 3) uniform float uNight; // 0 day .. 1 night

flat in uint  vKind;
in vec2       vTiles;
flat in vec2  vCellTiles;
flat in ivec2 vTile;
in vec2       vWall;
flat in vec4  vWallInfo;
flat in uvec2 vSeedSet;
flat in float vHeight;
out vec4      fragColor;

const vec2 kAtlas = vec2(27.0, 18.0);

uint hashU(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

void main() {
    float dim = 1.0 - 0.35 * uNight;

    if (vKind == 1u) { // ---- roof
        // Repeat the cell's tile; clamp just inside the far edge so it does not wrap.
        // Gradients from the continuous coordinates, else mipmapping breaks on every seam.
        vec2 t  = min(vTiles, vCellTiles - 1e-4);
        vec2 uv = (vec2(vTile) + fract(t)) / kAtlas;
        vec4 c  = textureGrad(uAtlas, uv, dFdx(vTiles) / kAtlas, dFdy(vTiles) / kAtlas);
        if (c.a < 0.35) discard;
        fragColor = vec4(c.rgb / max(c.a, 1e-3) * dim, 1.0);
        return;
    }

    // ---- wall
    float detail = smoothstep(2.0, 5.0, uPpm); // windows readable from ~3 px/m
    vec2  w      = (floor(vWall / 0.25) + 0.5) * 0.25;
    vec3  c       = vWallInfo.rgb;
    uint  pattern = vSeedSet.y; // 0 houses, 1 office bands, 2 shop fronts, 3 industrial
    bool  office  = pattern == 1u;

    float fl  = floor(w.y / 3.0), fy = w.y - fl * 3.0; // floor index, metres into the floor
    float bay = office ? 1.75 : pattern == 3u ? 3.5 : 2.5;
    float bi  = floor(w.x / bay), bx = w.x - bi * bay;
    bool  win;
    if (office)                         win = fy > 0.7 && fy < 2.6 && bx > 0.15 && bx < bay - 0.15; // glass bands
    else if (pattern == 3u)             win = w.y > vHeight - 2.2 && w.y < vHeight - 1.2 && bx > 0.3; // clerestory band
    else if (pattern == 2u && fl < 0.5) win = fy > 0.3 && fy < 2.5 && bx > 0.2 && bx < bay - 0.2;   // shop front
    else                                win = fy > 1.0 && fy < 2.25 && bx > 0.75 && bx < 1.75;      // punched windows
    win = win && w.x > 0.5 && w.x < vWallInfo.w - 0.5 && w.y < vHeight - 0.6; // not at the wall ends / parapet
    if (pattern == 3u && w.y < 4.0 && bx > 1.0 && bx < 2.5 && uint(bi) % 3u == 1u) c *= 0.55; // loading doors

    float lit = 0.0;
    if (win) {
        uint  h     = hashU(vSeedSet.x ^ (uint(bi) * 747796405u) ^ (uint(fl) * 2891336453u));
        lit         = float(h & 1023u) / 1023.0 < (office ? 0.25 : 0.55) ? 1.0 : 0.0;
        vec3  glass = mix(vec3(0.23, 0.29, 0.42), vec3(0.10, 0.11, 0.17), uNight);
        glass       = mix(glass, vec3(0.95, 0.74, 0.42), lit * uNight);
        c           = mix(c, glass, detail);
    }
    if (w.y < 0.35) c *= mix(1.0, 0.82, detail);
    fragColor = vec4(c * mix(dim, 1.0, lit * uNight * detail), 1.0);
}
