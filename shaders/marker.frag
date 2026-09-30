#version 460
#include "frame.glsl"
// The inspection marker: a bright ring with a soft pulsing glow.

in vec2  vLocal;
out vec4 fragColor;

void main() {
    float r    = length(vLocal);
    float aa   = fwidth(r);
    float ring = smoothstep(0.66 - aa, 0.66 + aa, r) * (1.0 - smoothstep(0.84 - aa, 0.84 + aa, r));
    float glow = exp(-pow((r - 0.75) / 0.18, 2.0)) * (0.5 + 0.3 * sin(fTime.y * 4.0));
    float a    = max(ring, glow * 0.6) * (1.0 - smoothstep(0.97, 1.0, r));
    if (a <= 0.0) discard;
    fragColor = vec4(overlayHex(0xffc04du) * (1.0 + 0.8 * ring), a);
}
