#version 460
#include "frame.glsl"
// A crown with a lumpy outline, shaded as a dome by the sun and sky; clumps of
// leaves darken and lighten it. With SHADOW defined: a soft blob multiplied onto
// the ground (only skylight is left in it, as for the buildings).

layout(location = 2) uniform float uFade;
layout(location = 3) uniform float uStrength;

in vec2       vLocal;
flat in uint  vInfo;
out vec4      fragColor;

void main() {
    uint  kind = vInfo & 3u, seed = vInfo >> 2u;
    float ang  = atan(vLocal.y, vLocal.x);
    float sd   = float(seed % 997u);
    float r    = length(vLocal);
    // Outline: lobes for broadleaf crowns, a star of branches for conifers.
    float edge = kind == 1u ? 0.92 + 0.10 * cos(ang * 8.0 + sd)
                            : 0.93 + 0.07 * sin(ang * 5.0 + sd) + 0.05 * sin(ang * 11.0 + sd * 1.7);
    float aa   = fwidth(r) * 1.2;
    float mask = 1.0 - smoothstep(edge - aa, edge + aa, r);
    if (mask <= 0.0) discard;
#ifdef SHADOW
    vec3 full  = fSun.rgb * max(fSun.a, 0.0) + fSky.rgb;
    vec3 ratio = fSky.rgb / max(full, vec3(1e-4));
    float soft = mask * (1.0 - smoothstep(0.5, edge, r) * 0.5);
    fragColor  = vec4(mix(vec3(1.0), ratio, uStrength * 0.75 * soft), 1.0);
#else
    vec2 l = vLocal / edge;
    vec3 n = normalize(vec3(l * 0.9, sqrt(max(1.0 - dot(l, l), 0.05))));
    // Leaf clumps: bumps on the normal and in brightness.
    float clump = fbm3(vLocal * 2.2 + sd);
    n = normalize(n + vec3(clump - 0.5, fbm3b(vLocal * 2.2 + sd) - 0.5, 0.0) * 0.6);
    uint  v = seed % 5u;
    const uint kLeaves[5] = uint[](0x5f8f45u, 0x77a352u, 0x86ad5au, 0x6a9a4cu, 0x9bb45fu);
    vec3  albedo = kind == 1u ? hexLin(v < 2u ? 0x3f6b45u : 0x4a7a4du)  // conifer
                 : kind == 2u ? hexLin(v < 3u ? 0x6e9d4du : 0x7faa56u)  // street tree
                              : hexLin(kLeaves[v]);
    albedo *= 0.8 + 0.4 * clump;
    vec3 c = lightN(albedo, n);
    c *= mix(0.72, 1.0, smoothstep(0.55, 0.95, n.z)); // self-shadowing towards the rim
    fragColor = vec4(c, mask * uFade);
#endif
}
