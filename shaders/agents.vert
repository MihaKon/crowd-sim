#version 460
#include "frame.glsl"
// Visible people as point sprites, drawn as small top-down figures in agents.frag.
// Facing, clothes and walk phase come from the visible entry and the id.

layout(std430, binding = 4) readonly buffer Visible { uint drawCmd[4]; uvec4 visible[]; }; // x,y,id,tag

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;     // 2 * ppm / viewport
layout(location = 2) uniform float uPointSize; // minimum, pixels
layout(location = 3) uniform float uPpm;
layout(location = 4) uniform uint  uCapacity;
layout(location = 5) uniform uint  uNow;       // ms, for the walk cycle

flat out vec3  vShirt;
flat out vec3  vHair;
flat out uint  vFacing; // 0 down (south), 1 right, 2 up, 3 left
flat out float vStep;   // -1..1 walk phase; 0 standing

// Clothes: mostly muted, some colour.
const uint kShirt[10] = uint[](0x2e3b55u, 0xe8e6e1u, 0x7d8490u, 0xc9b79au, 0x3f5e4cu, 0xa84a3eu,
                               0x3a7ca5u, 0xd9a441u, 0x5b4a6bu, 0x202226u);
const uint kHair[4] = uint[](0x1c1b1au, 0x3b2a20u, 0x6b4a2eu, 0x8a8580u);

void main() {
    if (uint(gl_VertexID) >= uCapacity) { // overflow guard: discard the point
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    uvec4 v    = visible[gl_VertexID];
    uint  id   = v.z;
    uint  tag  = v.w;
    uint  kind = (tag >> 4u) & 15u; // K_WAIT = 2: standing on the platform
    uint  h    = id * 2654435761u;

    vShirt  = hexLin(kShirt[(h >> 24u) % 10u]);
    vHair   = hexLin(kHair[(h >> 20u) % 4u]);
    vFacing = (tag >> 8u) & 3u;
    vStep   = kind == 2u ? 0.0 : sin(float(uNow % 100000u) * 0.018 + float(id % 1000u));

    vec2 p      = uintBitsToFloat(v.xy);
    gl_Position = vec4((p - uCenter) * uScale, 0.0, 1.0);
    // People are exaggerated to read: 1.35 m across the sprite (a 0.5 m person with
    // room for the shadow), never below the minimum, at most 32 px.
    gl_PointSize = clamp(1.35 * uPpm, max(uPointSize, 4.0), 32.0) * fView.y; // window pixels -> scene pixels
}
