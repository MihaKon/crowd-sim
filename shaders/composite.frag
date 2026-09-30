#version 460
// The only full-resolution post pass: HDR scene + bloom -> exposure -> ACES filmic
// -> grading -> sRGB, with FXAA folded in (after Lottes, "FXAA 3.11", console
// variant) on the HDR input, so no separate LDR buffer and pass are needed.

layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1) uniform sampler2D uBloom;
layout(location = 0) uniform vec2  uTexel;
layout(location = 2) uniform float uExposure;
layout(location = 3) uniform float uBloomAmount;
layout(location = 4) uniform float uVignette;
layout(location = 5) uniform float uWarmth;
layout(location = 6) uniform float uNight;

in vec2  vUv;
out vec4 fragColor;

// ACES filmic fit (Narkowicz 2015).
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 hdr(vec2 uv) { return texture(uScene, uv).rgb * uExposure; }
// Edge detection only needs a cheap perceptual brightness, not the full tone curve.
float luma(vec3 c) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    return sqrt(l / (1.0 + l));
}

// Returns the anti-aliased HDR colour (still to be tone mapped).
vec3 fxaa(vec3 mid) {
    vec2  t   = uTexel;
    float lNW = luma(hdr(vUv + vec2(-0.5, -0.5) * t)), lNE = luma(hdr(vUv + vec2(0.5, -0.5) * t));
    float lSW = luma(hdr(vUv + vec2(-0.5, 0.5) * t)), lSE = luma(hdr(vUv + vec2(0.5, 0.5) * t));
    float lM  = luma(mid);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.04, lMax * 0.125)) return mid; // no edge
    vec2  dir    = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);
    dir          = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-8.0), vec2(8.0)) * t;
    vec3  a  = 0.5 * (hdr(vUv + dir * (1.0 / 3.0 - 0.5)) + hdr(vUv + dir * (2.0 / 3.0 - 0.5)));
    vec3  b  = a * 0.5 + 0.25 * (hdr(vUv - dir * 0.5) + hdr(vUv + dir * 0.5));
    float lb = luma(b);
    return (lb < lMin || lb > lMax) ? a : b;
}

void main() {
    vec3 c = aces(fxaa(hdr(vUv)));
    if (uBloomAmount > 0.0) {
        // Bloom is added after tone mapping the base; mapping it through the same
        // curve keeps bright glows from clipping to flat white.
        vec3 bloom = texture(uBloom, vUv).rgb * uExposure * uBloomAmount;
        c = c + aces(bloom) * (1.0 - c);
    }

    // Grading: warm at golden hour, cool shadows at night, a little extra saturation.
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c       = mix(vec3(l), c, 1.08 + 0.12 * uWarmth);
    c      *= mix(vec3(1.0), vec3(1.06, 0.98, 0.90), uWarmth);
    c       = mix(c, c * vec3(0.95, 0.98, 1.05) + vec3(0.0, 0.002, 0.006), uNight * (1.0 - l));

    vec2 d = vUv - 0.5;
    c     *= 1.0 - uVignette * smoothstep(0.25, 0.85, dot(d, d) * 2.2);

    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    fragColor = vec4(c + n / 255.0, 1.0); // dither against banding in dark gradients
}
