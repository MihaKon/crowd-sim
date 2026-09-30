#version 460
#include "frame.glsl"
// One building = 36 vertices pulled from its record (18 in the lite version):
//   walls: the four walls, 6 each; only the ones facing the viewer (south) are kept,
//          the others collapse (they are behind the roof). Lite: the two visible ones.
//   roof:  two quads split along the ridge; pitched for small tiled roofs, else flat.
// Oblique 2.5D: a point at height z is drawn lifted(z) * kLean metres further north.

struct Building {
    vec2  origin;    // street-facing corner
    vec2  axisAlong; // unit, along the street
    float sizeAlong; // metres
    float sizeIn;
    uint  info;      // bits 0-2 roof style, bit 3: axisIn = -perp(axisAlong), bits 4-6 facade,
                     // bits 7-8 window pattern, bits 9-31 seed
    float height;    // metres
};
layout(std430, binding = 19) readonly buffer Buildings { Building buildings[]; };

layout(location = 0) uniform vec2 uCenter;
layout(location = 1) uniform vec2 uScale;
layout(location = 3) uniform int  uLite;

out vec2       vLocal;  // wall: metres along, metres up; roof: metres along, metres in
flat out uint  vKind;   // 0 wall, 1 roof
flat out vec3  vNormal; // x east, y north, z up
flat out vec4  vSize;   // sizeAlong, sizeIn, height, wall length
flat out uint  vInfo;
flat out float vPitch;  // ridge height above the eaves (0: flat roof)

const float kLean = 0.42; // must match building_render.cpp
float lifted(float h) { return h < 50.0 ? h : 50.0 + (h - 50.0) * 0.55; }
vec2  lift(float z) { return vec2(0.0, lifted(z) * kLean); }

const vec2 kQuad[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void hide() { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }

void main() {
    int      per = uLite != 0 ? 18 : 36;
    Building b   = buildings[gl_VertexID / per];
    int      k   = gl_VertexID % per;
    vec2     A   = b.axisAlong;
    vec2     I   = vec2(-A.y, A.x) * ((b.info & 8u) != 0u ? -1.0 : 1.0);
    uint     style = b.info & 7u;
    vec2     q     = kQuad[k % 6];

    vInfo  = b.info;
    vSize  = vec4(b.sizeAlong, b.sizeIn, b.height, 0.0);
    vLocal = vec2(0.0);
    // Tiled roofs of low buildings get a pitch across their shorter side.
    bool  pitched = style <= 1u && b.height <= 20.0 && uLite == 0;
    float short_  = min(b.sizeAlong, b.sizeIn);
    vPitch        = pitched ? min(0.32 * short_, 4.0) : 0.0;

    int walls = uLite != 0 ? 12 : 24;
    vec2 world;
    if (k < walls) { // ---- wall
        vKind = 0u;
        vec2 c[4] = vec2[](b.origin, b.origin + A * b.sizeAlong, b.origin + A * b.sizeAlong + I * b.sizeIn,
                           b.origin + I * b.sizeIn);
        vec2 centre = b.origin + A * (0.5 * b.sizeAlong) + I * (0.5 * b.sizeIn);
        int  e = k / 6;
        if (uLite != 0) { // the e-th wall that faces the viewer
            int found = -1, seen = 0;
            for (int i = 0; i < 4; ++i) {
                vec2 ed = c[(i + 1) % 4] - c[i], n = normalize(vec2(ed.y, -ed.x));
                if (dot(n, 0.5 * (c[i] + c[(i + 1) % 4]) - centre) < 0.0) n = -n;
                if (n.y < -0.02 && seen++ == e) found = i;
            }
            if (found < 0) { hide(); return; }
            e = found;
        }
        vec2 p0 = c[e], p1 = c[(e + 1) % 4];
        vec2 edge = p1 - p0;
        vec2 n = normalize(vec2(edge.y, -edge.x));
        if (dot(n, 0.5 * (p0 + p1) - centre) < 0.0) n = -n; // outward
        if (n.y > -0.02) { hide(); return; }                   // faces away: behind the roof
        world   = mix(p0, p1, q.x) + lift(b.height * q.y);
        vLocal  = q * vec2(length(edge), b.height);
        vNormal = vec3(n, 0.0);
        vSize.w = length(edge);
    } else { // ---- roof: quad 0 from the near edge to the ridge, quad 1 from the ridge on
        vKind = 1u;
        int   half_ = uLite != 0 ? 0 : (k - walls) / 6;
        bool  ridgeAlong = b.sizeAlong >= b.sizeIn; // ridge parallel to the longer side
        vec2  u = q;                                 // position in the roof, 0..1 both ways
        float w;                                     // 0 eave .. 1 ridge
        if (uLite != 0) {
            w = 0.0;
        } else if (ridgeAlong) {
            u.y = (float(half_) + q.y) * 0.5;
            w   = half_ == 0 ? q.y : 1.0 - q.y;
        } else {
            u = vec2((float(half_) + q.x) * 0.5, q.y);
            w = half_ == 0 ? q.x : 1.0 - q.x;
        }
        float z = b.height + vPitch * w;
        world   = b.origin + A * (u.x * b.sizeAlong) + I * (u.y * b.sizeIn) + lift(z);
        vLocal  = u * vec2(b.sizeAlong, b.sizeIn);
        // Slope normal: tilted away from the ridge.
        vec2  away = ridgeAlong ? (half_ == 0 ? -I : I) : (half_ == 0 ? -A : A);
        float run  = 0.5 * (ridgeAlong ? b.sizeIn : b.sizeAlong);
        vNormal    = vPitch > 0.0 ? normalize(vec3(away * vPitch, run)) : vec3(0.0, 0.0, 1.0);
    }
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
