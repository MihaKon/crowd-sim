#version 460
// Visible people as point sprites; gl_PointCoord selects the pixel inside the atlas tile.
// Facing, skin and walk frame come from the visible entry and the id, no extra buffers.

layout(std430, binding = 4) readonly buffer Visible { uint drawCmd[4]; uvec4 visible[]; }; // x,y,id,tag

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;     // 2 * ppm / viewport
layout(location = 2) uniform float uPointSize; // minimum, pixels
layout(location = 3) uniform float uPpm;
layout(location = 4) uniform uint  uCapacity;
layout(location = 5) uniform uint  uNow;       // ms, for the walk-cycle animation

flat out vec4 vUv; // tile rect: x0, y0, x1, y1

const float kAtlasCols = 27.0, kAtlasRows = 18.0;
// Character columns per facing (0 down, 1 right, 2 up, 3 left), verified on the
// sheet: 24 front and 25 back are left-right symmetric, 23 and 26 are mirror
// images of each other (profile facing left / right). Must match sprite_atlas.cpp.
const float kFacingCol[4] = float[](24.0, 26.0, 25.0, 23.0);

vec4 tileUv(float col, float row) {
    float x0 = col / kAtlasCols, y0 = row / kAtlasRows;
    return vec4(x0, y0, x0 + 1.0 / kAtlasCols, y0 + 1.0 / kAtlasRows);
}

void main() {
    if (uint(gl_VertexID) >= uCapacity) { // overflow guard: discard the point
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vUv         = vec4(0.0);
        return;
    }
    uvec4 v    = visible[gl_VertexID];
    uint  id   = v.z;
    uint  tag  = v.w;
    uint  kind = (tag >> 4u) & 15u; // K_WAIT = 2: standing on the platform
    uint  dir  = (tag >> 8u) & 3u;  // 0 down, 1 right, 2 up, 3 left

    uint skin  = (id * 2654435761u >> 24u) % 6u;
    uint frame = kind == 2u ? 0u : 1u + ((uNow / 350u + id) % 2u); // idle on the platform, else ~3 steps/s

    vUv = tileUv(kFacingCol[dir], float(skin) * 3.0 + float(frame));

    vec2 p      = uintBitsToFloat(v.xy);
    gl_Position = vec4((p - uCenter) * uScale, 0.0, 1.0);
    // Pixel art needs to be exaggerated to read: 1.6 m per 16 px tile (native 1:1 at
    // 10 px/m), never below 5 px, at most 2x native.
    gl_PointSize = clamp(1.6 * uPpm, max(uPointSize, 5.0), 32.0);
}
