#version 460
#include "frame.glsl"
// One tree = 6 vertices: a quad around the crown, lifted like the buildings (kLean),
// or with SHADOW defined, around the crown's shadow on the ground.

struct Tree {
    float x, y, radius;
    uint  info; // bits 0-1 kind (0 broadleaf, 1 conifer, 2 street), bits 2-31 seed
};
layout(std430, binding = 20) readonly buffer Trees { Tree trees[]; };

layout(location = 0) uniform vec2 uCenter;
layout(location = 1) uniform vec2 uScale;

out vec2      vLocal; // -1.2 .. 1.2 across the crown
flat out uint vInfo;

const float kLean = 0.42; // buildings.vert
const vec2  kQuad[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    Tree  t      = trees[gl_VertexID / 6];
    vec2  q      = kQuad[gl_VertexID % 6] * 1.2;
    float height = t.radius * (((t.info & 3u) == 1u) ? 3.2 : 2.3);
#ifdef SHADOW
    vec2 centre = vec2(t.x, t.y) + fSunDir.zw * height * 0.7;
#else
    vec2 centre = vec2(t.x, t.y) + vec2(0.0, height * kLean);
#endif
    vec2 world  = centre + q * t.radius;
    vLocal      = q;
    vInfo       = t.info;
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
