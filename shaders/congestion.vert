#version 460
#include "gpu_layout.h"
// Traffic layer: every lane with cars as a line coloured by how jammed it is
// (laneStat, written by traffic.comp). Both directions side by side; lines
// keep a minimum width in pixels so the pattern reads at city scale.

layout(std430, binding = 3)  readonly buffer World    { uint W[]; };
layout(std430, binding = 11) readonly buffer Traffic  { uint T[]; };
layout(std430, binding = 20) readonly buffer LaneStat { uint laneStat[]; };

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;
layout(location = 2) uniform float uPpm;
layout(location = 3) uniform uint  uNodeSec; // SimWorld kSecNodePos
layout(location = 4) uniform uint  uLaneSec; // TrafficWorld kTSecLanes
layout(location = 5) uniform float uOpacity;

out vec4 vColor;

const vec2 kQuad[6] = vec2[](vec2(0, -0.5), vec2(1, -0.5), vec2(1, 0.5), vec2(0, -0.5), vec2(1, 0.5), vec2(0, 0.5));
const vec3 kFree = vec3(0.620, 0.808, 0.416); // #9ece6a
const vec3 kSlow = vec3(0.878, 0.686, 0.408); // #e0af68
const vec3 kJam  = vec3(0.969, 0.463, 0.557); // #f7768e

vec2 node(uint n) {
    uint o = uNodeSec + 2u * n;
    return vec2(uintBitsToFloat(W[o]), uintBitsToFloat(W[o + 1u]));
}

void main() {
    uint  l   = uint(gl_VertexID) / 6u;
    uint  st  = laneStat[l];
    uint  o   = uLaneSec + uint(LANE_STRIDE) * l;
    bool  art = uintBitsToFloat(T[o + 5u]) > 10.0;
    bool  empty = (st >> 16u) == 0u;
    vColor = vec4(0.0);
    if ((empty && !art) || uOpacity <= 0.0) { // empty side street: nothing to show
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec2  a    = node(T[o]), d = node(T[o + 1u]) - a;
    uint  lane = (T[o + 6u] >> 2u) & 3u;
    vec2  dir  = d / max(length(d), 1e-3), left = vec2(-dir.y, dir.x);

    float w   = max(art ? 3.5 : 3.0, (art ? 3.0 : 1.6) / uPpm);          // metres, >= 3 / 1.6 px
    float off = max(art ? 2.0 + 4.0 * float(lane) : 1.75, 0.5 * w + (art ? w * float(lane) : 0.0)); // keep left
    vec2  q   = kQuad[gl_VertexID % 6];
    vec2  world = a + d * q.x + left * (off + q.y * w);

    float jam = float(st & 0xFFFFu) / 65535.0;
    vColor    = vec4(jam < 0.5 ? mix(kFree, kSlow, jam * 2.0) : mix(kSlow, kJam, jam * 2.0 - 1.0),
                     uOpacity * (empty ? 0.25 : 1.0)); // empty arterials faint, so the network reads
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
