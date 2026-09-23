#version 460
// Density map: one quad over the whole city, coloured per fragment.

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;
layout(location = 2) uniform float uSize; // city side, metres

out vec2 vWorld;

const vec2 kCorner[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
    vWorld      = kCorner[gl_VertexID] * uSize;
    gl_Position = vec4((vWorld - uCenter) * uScale, 0.0, 1.0);
}
