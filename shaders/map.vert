#version 460

layout(location = 0) in vec2  aPos;
layout(location = 1) in vec2  aExtrude; // unit direction, or (±1, ±1) for squares
layout(location = 2) in float aMinPx;
layout(location = 3) in float aWidth;   // metres
layout(location = 4) in vec4  aColor;

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale; // 2 * ppm / viewport
layout(location = 2) uniform float uPpm;

out vec4 vColor;

void main() {
    // Real width when zoomed in, but never thinner than aMinPx pixels.
    float wPx   = max(aWidth * uPpm, aMinPx);
    vec2  world = aPos + aExtrude * (0.5 * wPx / uPpm);
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
    vColor      = aColor;
}
