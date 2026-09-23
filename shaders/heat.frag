#version 460
// Bilinear density from the count grid, log scale around the average,
// Tokyo Night colour ramp; sparse cells stay transparent.

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
    const vec3 c0 = vec3(0.239, 0.349, 0.631); // #3d59a1
    const vec3 c1 = vec3(0.478, 0.635, 0.969); // #7aa2f7
    const vec3 c2 = vec3(0.733, 0.604, 0.969); // #bb9af7
    const vec3 c3 = vec3(0.969, 0.463, 0.557); // #f7768e
    const vec3 c4 = vec3(1.000, 0.620, 0.392); // #ff9e64
    const vec3 c5 = vec3(0.878, 0.686, 0.408); // #e0af68
    t *= 5.0;
    if (t < 1.0) return mix(c0, c1, t);
    if (t < 2.0) return mix(c1, c2, t - 1.0);
    if (t < 3.0) return mix(c2, c3, t - 2.0);
    if (t < 4.0) return mix(c3, c4, t - 3.0);
    return mix(c4, c5, min(t - 4.0, 1.0));
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
    fragColor = vec4(ramp(t), a);
}
