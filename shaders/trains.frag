#version 460
#include "frame.glsl"
// Ten stainless carriages with a stripe in the line colour; the roof strip shows
// how full the train is (line colour from the rear up to the load). Lit windows at
// night, a headlight at the front.

in float      vAlong;
in float      vAcross;
flat in float vLoad;
flat in vec3  vColor;
flat in float vLen;
out vec4      fragColor;

void main() {
    float carLen = vLen / 10.0;
    float x      = vAlong * vLen;            // metres from the rear
    float inCar  = x - floor(x / carLen) * carLen;
    float gap    = 0.8 * carLen / 20.0;      // coupling gap, scaled with the train
    float aa     = fwidth(inCar) * 1.2;
    float body   = smoothstep(gap * 0.5 - aa, gap * 0.5 + aa, inCar) *
                   (1.0 - smoothstep(carLen - gap * 0.5 - aa, carLen - gap * 0.5 + aa, inCar));
    if (body <= 0.0) discard;

    float a   = abs(vAcross);
    vec3  c   = hexLin(0xd5d9dfu);                                   // stainless roof
    c         = mix(c, vColor, step(0.62, a) * step(a, 0.86));       // side stripe
    c         = mix(c, hexLin(0x9ea4ad), step(a, 0.22));              // roof walkway
    float load = step(vAlong, vLoad);
    c = mix(c, mix(hexLin(0xb9bec6u), vColor, load), step(a, 0.16));  // load strip
    // Roof units, two per car.
    float u = abs(inCar / carLen - 0.5);
    c = mix(c, hexLin(0xc3c8cfu), step(abs(u - 0.25), 0.05) * step(a, 0.5) * (1.0 - step(a, 0.22)));

    vec3 n   = normalize(vec3(0.0, vAcross * 0.45, 1.0));
    vec3 lit = lightN(c, n);
    // Night: warm windows along the sides, busier trains brighter; a headlight in front.
    float win = step(0.86, a) * step(0.2, fract(inCar / 1.6));
    lit += vec3(1.0, 0.86, 0.62) * 1.6 * win * lampsF() * (0.4 + 0.6 * vLoad);
    lit += vec3(1.0, 0.95, 0.85) * 8.0 * lampsF() * smoothstep(vLen - 0.8, vLen, x) * step(a, 0.6);
    fragColor = vec4(lit, body);
}
