#version 460
#include "frame.glsl"
// Top-down cars: a rounded body in its paint, windscreens, roof highlight, lights,
// and a soft shadow. With HEADLIGHTS: two warm beams ahead and a red glow behind,
// added to the scene at night.

in vec2       vLocal;
flat in vec2  vHalf;
flat in uint  vVariant;
flat in float vBraking;
out vec4      fragColor;

// Signed distance to a rounded box.
float roundBox(vec2 p, vec2 half_, float r) {
    vec2 q = abs(p) - half_ + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// white, silver, black, navy, red, moss; taxi; vans
const uint kPaint[9] = uint[](0xf2f2f0u, 0xb8bcc2u, 0x26282du, 0x2f4a73u, 0xa8322du, 0x5e6b55u,
                              0x202a40u, 0xecece8u, 0xd9dde2u);

void main() {
    vec2  p = vLocal / vHalf; // -1..1 over the body (x forward)
#ifdef HEADLIGHTS
    float lamps = lampsF();
    vec3  c = vec3(0.0);
    // Beams: from the two headlights, spreading forward, fading with distance.
    float ahead = vLocal.x - vHalf.x;
    if (ahead > 0.0) {
        for (int k = 0; k < 2; ++k) {
            float y0 = (k == 0 ? 0.65 : -0.65) * vHalf.y;
            float spread = 0.9 + ahead * 0.28;
            float across = (vLocal.y - y0) / spread;
            c += vec3(1.0, 0.86, 0.62) * 0.55 * exp(-across * across * 2.0) * exp(-ahead / 11.0) *
                 smoothstep(0.0, 1.5, ahead);
        }
    }
    // Tail lights: a small red glow, brighter while braking.
    float behind = -vLocal.x - vHalf.x;
    if (behind > -0.3) {
        float g = exp(-max(behind, 0.0) * 1.5) * exp(-pow(vLocal.y / (vHalf.y + 0.4), 2.0) * 3.0);
        c += vec3(1.0, 0.1, 0.06) * g * (0.35 + 1.2 * vBraking);
    }
    fragColor = vec4(c * lamps, 1.0);
#else
    float px   = fwidth(vLocal.x);
    float d    = roundBox(vLocal, vHalf, 0.45 * vHalf.y);
    float body = 1.0 - smoothstep(-px, px, d);
    // Soft shadow on the road, a little towards the shadow side.
    float sd     = roundBox(vLocal - fSunDir.zw * 0.35 * vHalf.y, vHalf, 0.45 * vHalf.y);
    float shadow = (1.0 - smoothstep(-0.2, 0.9, sd)) * 0.45 * step(0.0, fSun.a) +
                   (1.0 - smoothstep(-0.3, 0.6, d)) * 0.15; // contact shadow, also at night
    if (body <= 0.0 && shadow <= 0.0) discard;

    bool van  = vVariant >= 7u;
    vec3 paint = hexLin(kPaint[vVariant]);
    vec3 glass = hexLin(0x1d2530u) + fSky.rgb * 0.12;
    vec3 c     = paint;
    // Windscreen, rear window, side windows; the roof in between.
    float front = van ? 0.46 : 0.30, rear = van ? -2.0 : -0.62;
    float onWin = step(front - 0.26, p.x) * step(p.x, front) + (van ? 0.0 : step(rear - 0.18, p.x) * step(p.x, rear));
    float sides = step(0.62, abs(p.y)) * step(rear, p.x) * step(p.x, front) * (van ? step(0.2, p.x) : 1.0);
    c = mix(c, glass, clamp(onWin + sides * 0.8, 0.0, 1.0) * step(abs(p.y), 0.86));
    if (vVariant == 6u) c = mix(c, hexLin(0xf2b33du), step(abs(p.x + 0.1), 0.1) * step(abs(p.y), 0.35)); // sign
    if (van) c = mix(c, c * 0.9, step(abs(fract(p.x * 3.0 + 0.5) - 0.5), 0.03) * step(p.x, 0.16)); // box seams
    // Rounded shading: darker towards the edges, a highlight along the roof.
    float edgeShade = smoothstep(-0.9 * vHalf.y, 0.0, d);
    vec3  n = normalize(vec3(-normalize(vLocal + 1e-4) * edgeShade * 0.8, 1.0));
    vec3  lit = lightN(c, n);
    // Lights: headlights at the nose, tail lights at the back (bright when braking).
    float hl = step(0.86, p.x) * step(0.35, abs(p.y)) * step(abs(p.y), 0.85);
    float tl = step(p.x, -0.9) * step(0.4, abs(p.y)) * step(abs(p.y), 0.85);
    lit = mix(lit, vec3(1.0, 0.95, 0.8) * (0.6 + 6.0 * lampsF()), hl * 0.9);
    lit = mix(lit, vec3(0.9, 0.06, 0.05) * (0.4 + (1.5 + 4.0 * vBraking) * lampsF() + vBraking * 0.6), tl);
    // Outside the body: the shadow, darkening the road (alpha blended black).
    fragColor = body > 0.0 ? vec4(lit, body) : vec4(vec3(0.0), shadow);
#endif
}
