#version 460
// A building's shadow on the ground, sun from the north-west: the footprint,
// the footprint moved by the shadow vector, and each edge swept along it
// (together: the convex hull). Drawn with the stencil test so overlapping
// quads and shadows darken each pixel only once (see building_render.cpp).

struct Building {
    vec2  origin;
    vec2  axisAlong;
    float sizeAlong;
    float sizeIn;
    uint  info;
    float height;
};
layout(std430, binding = 19) readonly buffer Buildings { Building buildings[]; };

layout(location = 0) uniform vec2 uCenter;
layout(location = 1) uniform vec2 uScale;

const vec2 kQuad[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
    Building b = buildings[gl_VertexID / 36];
    int      k = gl_VertexID % 36, quad = k / 6;
    vec2     q = kQuad[k % 6];
    vec2     A = b.axisAlong;
    vec2     I = vec2(-A.y, A.x) * ((b.info & 8u) != 0u ? -1.0 : 1.0);
    vec2     s = vec2(0.55, -0.35) * b.height; // to the south-east

    vec2 c[4] = vec2[](b.origin, b.origin + A * b.sizeAlong, b.origin + A * b.sizeAlong + I * b.sizeIn,
                       b.origin + I * b.sizeIn);
    vec2 world;
    if (quad < 2) world = b.origin + A * (b.sizeAlong * q.x) + I * (b.sizeIn * q.y) + s * float(quad);
    else          world = mix(c[quad - 2], c[(quad - 1) % 4], q.x) + s * q.y;
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
