#version 460

layout(location = 0) in vec2  aPos;
layout(location = 1) in vec2  aExtrude; // unit direction, or (±1, ±1) for squares
layout(location = 2) in uvec2 aInfo;    // min width in pixels, material
layout(location = 3) in float aWidth;   // metres
layout(location = 4) in uint  aColor;
layout(location = 5) in vec2  aParam;

layout(location = 0) uniform vec2  uCenter;
layout(location = 1) uniform vec2  uScale; // 2 * ppm / viewport
layout(location = 2) uniform float uPpm;

out vec2       vWorld;
out float      vParam;
out float      vParam2;
flat out uint  vMaterial;
flat out uint  vColor;

void main() {
    // Real width when zoomed in, but never thinner than the minimum in pixels.
    float wPx   = max(aWidth * uPpm, float(aInfo.x));
    vec2  world = aPos + aExtrude * (0.5 * wPx / uPpm);
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
    vWorld      = world;
    vParam      = aParam.x;
    vParam2     = aParam.y;
    vMaterial   = aInfo.y;
    vColor      = aColor;
}
