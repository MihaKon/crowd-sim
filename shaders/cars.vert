#version 460
// Cars from the visible list written by traffic.comp, drawn as rotated quads
// with the top-down pixel-art sprites built in sprite_atlas.cpp (nose up).

layout(std430, binding = 15) readonly buffer CarVisible { uint carCmd[4]; uvec4 carVis[]; }; // x,y,id,heading|speed<<16

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;
layout(location = 2) uniform float uPpm;
layout(location = 3) uniform uint  uCapacity;
layout(location = 4) uniform uint  uOwnerCount; // cars [0, ownerCount) are agents' private cars

out vec2       vUv;
flat out float vBraking; // 1: stopped / slowing hard, light the tail lights

const vec2 kCorner[6] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
                               vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
const float kVariants = 9.0; // SpriteAtlas::kCarVariants

void main() {
    uint i = uint(gl_VertexID) / 6u;
    if (i >= uCapacity) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vUv      = vec2(0.0);
        vBraking = 0.0;
        return;
    }
    uvec4 v       = carVis[i];
    vec2  pos     = uintBitsToFloat(v.xy);
    uint  id      = v.z;
    float heading = float(v.w & 0xFFFFu) / 65535.0 * 6.2831853 - 3.14159265;
    float speed   = float(v.w >> 16u) / 65535.0; // relative to the speed limit

    // Variant (see SpriteAtlas::buildCars): private 0-5 weighted towards white /
    // silver / black; fleet: 70% vans (7, 8), 30% taxis (6), as in traffic.comp.
    uint variant;
    if (id >= uOwnerCount) {
        uint f  = id - uOwnerCount;
        variant = f % 10u < 7u ? 7u + (f / 10u) % 2u : 6u;
    } else {
        uint h  = (id * 2654435761u) >> 24u; // 0..255
        variant = h < 90u ? 0u : h < 150u ? 1u : h < 200u ? 2u : 3u + h % 3u;
    }

    // Sprite 12 x 24 px -> 2.6 x 5.2 m; on screen at least 16 px long close up,
    // shrinking to 6 px as the view zooms out (else cars swamp the streets).
    vec2  dir  = vec2(cos(heading), sin(heading)), side = vec2(-dir.y, dir.x);
    float len  = max(5.2, clamp(40.0 * uPpm, 6.0, 16.0) / uPpm), wid = len * 0.5;
    vec2  c    = kCorner[gl_VertexID % 6];
    vec2  world = pos + dir * (c.x * len) + side * (c.y * wid);

    // Car space -> sprite: nose (c.x = +0.5) at the top row (v = 0), car's left
    // (c.y = +0.5) on the sprite's left (u = 0).
    vUv      = vec2((float(variant) + 0.5 - c.y) / kVariants, 0.5 - c.x);
    vBraking = speed < 0.15 ? 1.0 : 0.0;

    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
