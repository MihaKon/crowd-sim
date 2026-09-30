#version 460
#include "frame.glsl"
// Bilinear density from the count grid, log scale around the average, indigo to
// coral; sparse cells stay transparent. The hottest spots glow a little.

layout(std430, binding = 18) readonly buffer Heat { uint heat[]; };

layout(location = 2) uniform float uSize;
layout(location = 3) uniform uint  uDim;
layout(location = 4) uniform float uAverage; // agents per cell on land
layout(location = 5) uniform float uOpacity;

in vec2  vWorld;
out vec4 fragColor;

float cell(ivec2 c) {
    c = clamp(c, ivec2(0), ivec2(int(uDim) - 1));
    return float(heat[uint(c.y) * uDim + uint(c.x)]);
}

vec3 ramp(float t) {
    const uint k[6] = uint[](0x2f2f7au, 0x3a6fd8u, 0x36c0d2u, 0xf1d54cu, 0xf28c38u, 0xe8455au);
    t *= 5.0;
    int i = min(int(t), 4);
    return mix(overlayHex(k[i]), overlayHex(k[i + 1]), clamp(t - float(i), 0.0, 1.0));
}

float bilinear(vec2 g) {
    ivec2 i = ivec2(floor(g));
    vec2  f = g - vec2(i);
    return mix(mix(cell(i), cell(i + ivec2(1, 0)), f.x), mix(cell(i + ivec2(0, 1)), cell(i + ivec2(1, 1)), f.x), f.y);
}

void main() {
    // Light blur: centre + 4 bilinear taps 0.7 cells away.
    vec2  g = vWorld / uSize * float(uDim) - 0.5;
    float v = 0.4 * bilinear(g) + 0.15 * (bilinear(g + vec2(0.7, 0.0)) + bilinear(g - vec2(0.7, 0.0)) +
                                          bilinear(g + vec2(0.0, 0.7)) + bilinear(g - vec2(0.0, 0.7)));

    // 0 at nobody, 0.5 around 3x the average, 1 at ~40x.
    float t = clamp(log(1.0 + v / max(uAverage, 1e-3)) / log(41.0), 0.0, 1.0);
    float a = smoothstep(0.02, 0.2, t) * uOpacity * 0.9;
    if (a < 0.003) discard;
    fragColor = vec4(ramp(t) * (1.0 + 1.5 * smoothstep(0.75, 1.0, t)), a);
}
