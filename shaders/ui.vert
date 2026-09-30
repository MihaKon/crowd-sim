#version 460
// Screen-space UI: text from the font atlas, flat and rounded rectangles.

layout(location = 0) in vec2 aPos;   // framebuffer pixels, y down
layout(location = 1) in vec2 aUv;    // atlas uv (x < 0: solid colour), or pixels from a shape's centre
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aShape; // rounded rect: half size, radius, feather; x = 0: not a shape

layout(location = 0) uniform vec2 uViewport;

out vec2      vUv;
out vec4      vColor;
flat out vec4 vShape;

void main() {
    vUv         = aUv;
    vColor      = aColor;
    vShape      = aShape;
    gl_Position = vec4(aPos.x / uViewport.x * 2.0 - 1.0, 1.0 - aPos.y / uViewport.y * 2.0, 0.0, 1.0);
}
