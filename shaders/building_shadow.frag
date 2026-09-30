#version 460
#include "frame.glsl"
// Multiplied onto the ground: in the shadow only the skylight is left, so shadows
// are bluish by day and warm-tinted around sunset.

layout(location = 5) uniform float uStrength;

out vec4 fragColor;

void main() {
    vec3 full  = fSun.rgb * max(fSun.a, 0.0) + fSky.rgb;
    vec3 ratio = fSky.rgb / max(full, vec3(1e-4));
    fragColor  = vec4(mix(vec3(1.0), ratio, uStrength * 0.85), 1.0);
}
