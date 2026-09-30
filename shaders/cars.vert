#version 460
#include "frame.glsl"
// Cars from the visible list written by traffic.comp, drawn as rotated quads and
// shaded procedurally (cars.frag). With HEADLIGHTS defined the quad reaches far
// ahead of the car for the beams, drawn additively before the cars at night.

layout(std430, binding = 15) readonly buffer CarVisible { uint carCmd[4]; uvec4 carVis[]; }; // x,y,id,heading|speed<<16

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;
layout(location = 2) uniform float uPpm;
layout(location = 3) uniform uint  uCapacity;
layout(location = 4) uniform uint  uOwnerCount; // cars [0, ownerCount) are agents' private cars

out vec2       vLocal;   // metres: x along (front +), y across (left +)
flat out vec2  vHalf;    // half length, half width (metres)
flat out uint  vVariant; // 0-5 private, 6 taxi, 7-8 vans
flat out float vBraking; // 1: stopped / slowing hard

const vec2 kCorner[6] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
                               vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
const float kBeam = 24.0; // metres of headlight beam ahead

void main() {
    uint i = uint(gl_VertexID) / 6u;
    if (i >= uCapacity) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    uvec4 v       = carVis[i];
    vec2  pos     = uintBitsToFloat(v.xy);
    uint  id      = v.z;
    float heading = float(v.w & 0xFFFFu) / 65535.0 * 6.2831853 - 3.14159265;
    float speed   = float(v.w >> 16u) / 65535.0; // relative to the speed limit

    // Variant: private cars weighted towards white / silver / black; fleet: 70% vans, 30% taxis (traffic.comp).
    uint variant;
    if (id >= uOwnerCount) {
        uint f  = id - uOwnerCount;
        variant = f % 10u < 7u ? 7u + (f / 10u) % 2u : 6u;
    } else {
        uint h  = (id * 2654435761u) >> 24u; // 0..255
        variant = h < 90u ? 0u : h < 150u ? 1u : h < 200u ? 2u : 3u + h % 3u;
    }

    // Real size close up (4.6 x 1.9 m, vans 5 m); far away at least ~6 px long so
    // traffic stays visible, but never so big that cars swamp the streets.
    float realLen = variant >= 7u ? 5.0 : 4.6;
    float len = max(realLen, clamp(40.0 * uPpm, 6.0, 16.0) / uPpm * (realLen / 5.2));
    float wid = len * 0.41;
    vec2  dir = vec2(cos(heading), sin(heading)), side = vec2(-dir.y, dir.x);
    vec2  c   = kCorner[gl_VertexID % 6];
#ifdef HEADLIGHTS
    // From just behind the car (tail-light glow) to the end of the beam, and wider.
    float back = 0.5 * len + 2.0, front = 0.5 * len + kBeam, half_ = 0.5 * wid + 5.0;
    vec2  local = vec2(mix(-back, front, c.x + 0.5), c.y * 2.0 * half_);
#else
    // A little larger than the car, for the soft shadow and anti-aliased edges.
    vec2 local = c * vec2(len + 1.4, wid + 1.4);
#endif
    vec2 world = pos + dir * local.x + side * local.y;

    vLocal      = local;
    vHalf       = vec2(0.5 * len, 0.5 * wid);
    vVariant    = variant;
    vBraking    = speed < 0.15 ? 1.0 : 0.0;
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
