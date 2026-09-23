#version 460
// Ring around the inspected person / car. Position written by agents.comp / traffic.comp.

layout(std430, binding = 16) readonly buffer Select { uvec4 selAgent; uvec4 selCar; };

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;
layout(location = 2) uniform float uPpm;
layout(location = 3) uniform float uSizePx;

out vec2 vLocal; // -1..1

const vec2 kCorner[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    uvec4 s = selCar.z != 0u ? selCar : selAgent;
    vLocal  = kCorner[gl_VertexID];
    if (s.z == 0u) { // nothing to show
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec2 world  = uintBitsToFloat(s.xy) + vLocal * (0.5 * uSizePx / uPpm);
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
