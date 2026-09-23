#version 460
#include "gpu_layout.h"
// One instance per train slot; 6 vertices form a rectangle along the track.

layout(std430, binding = 3) readonly buffer World     { uint W[]; };
layout(std430, binding = 6) readonly buffer Occupancy { uint occupancy[]; };

layout(location = 0) in uvec2 aSlot; // line direction, slot

layout(location = TRAINS_LOC_CENTER)     uniform vec2  uCenter;
layout(location = TRAINS_LOC_SCALE)      uniform vec2  uScale; // 2 * ppm / viewport
layout(location = TRAINS_LOC_PPM)        uniform float uPpm;
layout(location = TRAINS_LOC_DAY_OFFSET) uniform uint  uDayOffset;
layout(location = TRAINS_LOC_NOW)        uniform uint  uNow;
layout(location = TRAINS_LOC_TRAIN_CAP) uniform uint  uTrainCap;
layout(location = LOC_COUNTS) uniform uvec4 uCounts;
layout(location = LOC_SEC)    uniform uint  uSec[SECTION_COUNT];
layout(location = TRAINS_LOC_LINE_COLOR) uniform vec3 uLineColor[TRAIN_LINE_COLORS];

out float      vAlong; // 0 = rear, 1 = front
flat out float vLoad;
flat out vec3  vColor;

#include "common.glsl"

const vec2 kCorner[6] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
                               vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));

void main() {
    float arc;
    uint  trip;
    if (!trainAt(aSlot.x, aSlot.y, uNow, arc, trip)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // not running: discard
        vAlong = 0.0;
        vLoad  = 0.0;
        vColor = vec3(0.0);
        return;
    }
    uint line = lineDirA(aSlot.x).w;
    vec2 tangent;
    vec2 p = railPos(line, arc, tangent);
    if ((aSlot.x & 1u) == 1u) tangent = -tangent; // direction 1 runs towards decreasing arc
    vec2 side = vec2(-tangent.y, tangent.x);
    p += side * 5.0; // keep left: one track per direction

    float len   = max(200.0, 14.0 / uPpm); // 10 cars, at least 14 px
    float wid   = max(7.0, 5.0 / uPpm);
    vec2  c     = kCorner[gl_VertexID];
    vec2  world = p + tangent * (c.x * len) + side * (c.y * wid);

    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
    vAlong      = c.x + 0.5;
    vLoad       = clamp(float(occupancy[trip]) / float(uTrainCap), 0.0, 1.0);
    vColor      = uLineColor[line % uint(TRAIN_LINE_COLORS)];
}
