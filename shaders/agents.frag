#version 460
#include "frame.glsl"
// A person seen from above: shoulders (shirt), head (hair), a soft shadow; arms
// swing while walking. The sprite is a point, so its y runs down the screen.

flat in vec3  vShirt;
flat in vec3  vHair;
flat in uint  vFacing;
flat in float vStep;
out vec4      fragColor;

float ellipse(vec2 p, vec2 r) { return length(p / r) - 1.0; } // < 0 inside (approximate)

void main() {
    vec2 p = gl_PointCoord * 2.0 - 1.0; // -1..1, y down
    p.y    = -p.y;                      // y up, like the world
    // Facing as a screen direction; shoulders across it.
    vec2 f  = vFacing == 0u ? vec2(0, -1) : vFacing == 1u ? vec2(1, 0) : vFacing == 2u ? vec2(0, 1) : vec2(-1, 0);
    vec2 s  = vec2(-f.y, f.x);
    vec2 q  = vec2(dot(p, s), dot(p, f)); // x across the shoulders, y forward

    float aa   = fwidth(p.x) * 1.2;
    float body = 1.0 - smoothstep(-aa, aa, ellipse(q - vec2(0.0, -0.05), vec2(0.62, 0.34)));
    // Arms swing opposite to each other while walking.
    float arms = 0.0;
    for (int k = 0; k < 2; ++k) {
        float side = k == 0 ? -1.0 : 1.0;
        vec2  a    = vec2(0.62 * side, 0.12 * vStep * side);
        arms = max(arms, 1.0 - smoothstep(-aa, aa, ellipse(q - a, vec2(0.16, 0.24))));
    }
    float head = 1.0 - smoothstep(-aa, aa, length(q - vec2(0.0, 0.08)) - 0.3);
    vec2  so   = p - fSunDir.zw * 0.25;
    float shadow = (1.0 - smoothstep(0.2, 0.9, length(so / vec2(0.8, 0.8)))) * 0.35;

    float cover = max(max(body, arms), head);
    if (cover <= 0.0 && shadow <= 0.01) discard;
    vec3 c = mix(vShirt, vShirt * 0.85, arms * (1.0 - body));
    c      = mix(c, vHair, head);
    vec3 n = normalize(vec3(q.x * 0.5, 0.0, 1.0));
    vec3 lit = lightN(c, n) + c * vec3(1.0, 0.75, 0.5) * 0.25 * lampsF(); // street light
    fragColor = cover > 0.0 ? vec4(lit, cover) : vec4(vec3(0.0), shadow);
}
