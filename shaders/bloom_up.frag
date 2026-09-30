#version 460
// Bloom upsample: 3x3 tent filter, added onto the next larger level.

layout(binding = 0) uniform sampler2D uSrc;
layout(location = 0) uniform vec2 uTexel; // of the source

in vec2  vUv;
out vec4 fragColor;

void main() {
    vec2 t = uTexel;
    vec3 s = texture(uSrc, vUv).rgb * 4.0;
    s += (texture(uSrc, vUv + vec2(-t.x, 0)).rgb + texture(uSrc, vUv + vec2(t.x, 0)).rgb +
          texture(uSrc, vUv + vec2(0, -t.y)).rgb + texture(uSrc, vUv + vec2(0, t.y)).rgb) * 2.0;
    s += texture(uSrc, vUv + vec2(-t.x, -t.y)).rgb + texture(uSrc, vUv + vec2(t.x, -t.y)).rgb +
         texture(uSrc, vUv + vec2(-t.x, t.y)).rgb + texture(uSrc, vUv + vec2(t.x, t.y)).rgb;
    fragColor = vec4(s / 16.0, 1.0);
}
