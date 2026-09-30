#version 460
// Bloom downsample (13 taps, as in Jimenez 2014, "Next generation post processing
// in Call of Duty"); the first pass is a cheap box filter that keeps only what is brighter than white.

layout(binding = 0) uniform sampler2D uSrc;
layout(location = 0) uniform vec2 uTexel;    // of the source
layout(location = 1) uniform int  uPrefilter;

in vec2  vUv;
out vec4 fragColor;

vec3 tap(vec2 o) { return texture(uSrc, vUv + o * uTexel).rgb; }

void main() {
    if (uPrefilter != 0) {
        // Full resolution in: 4 bilinear taps (a 4x4 box) are enough and far cheaper.
        vec3 s = 0.25 * (tap(vec2(-1, -1)) + tap(vec2(1, -1)) + tap(vec2(-1, 1)) + tap(vec2(1, 1)));
        // Soft knee around 1.0: bright lights bloom, lit surfaces barely do.
        float br   = max(s.r, max(s.g, s.b));
        float soft = clamp(br - 0.6, 0.0, 1.0);
        soft       = soft * soft * 0.5;
        s *= max(soft, br - 1.1) / max(br, 1e-4);
        fragColor = vec4(min(s, vec3(40.0)), 1.0); // no single-pixel fireflies
        return;
    }
    vec3 a = tap(vec2(-2, 2)), b = tap(vec2(0, 2)), c = tap(vec2(2, 2));
    vec3 d = tap(vec2(-2, 0)), e = tap(vec2(0, 0)), f = tap(vec2(2, 0));
    vec3 g = tap(vec2(-2, -2)), h = tap(vec2(0, -2)), i = tap(vec2(2, -2));
    vec3 j = tap(vec2(-1, 1)), k = tap(vec2(1, 1)), l = tap(vec2(-1, -1)), m = tap(vec2(1, -1));
    vec3 s = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    fragColor = vec4(s, 1.0);
}
