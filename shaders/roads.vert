#version 460

layout(location = 0) in vec2  aPos;
layout(location = 1) in float aAlong;
layout(location = 2) in float aLength;
layout(location = 3) in float aWidth;
layout(location = 4) in vec2  aExtrude;
layout(location = 5) in uvec4 aInfo;  // kind, flags, trimA, trimB
layout(location = 6) in uvec2 aExtra; // minPx, side

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale;
layout(location = 2) uniform float uPpm;

out vec2       vLocal;  // streets: (metres along, -1..1 across, +1 = left); junctions: -1..1 corner
out vec2       vWorld;
flat out uvec4 vInfo;
flat out float vLength; // street length / junction carriageway radius
flat out float vWidth;  // full width, metres

void main() {
    float wPx   = max(aWidth * uPpm, float(aExtra.x)); // real width, but at least minPx
    vec2  world = aPos + aExtrude * (0.5 * wPx / uPpm);
    vLocal      = aInfo.x == 2u ? aExtrude : vec2(aAlong, aExtra.y == 1u ? 1.0 : -1.0);
    vWorld      = world;
    vInfo       = aInfo;
    vLength     = aLength;
    vWidth      = aWidth;
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
