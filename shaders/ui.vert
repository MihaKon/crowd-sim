#version 460
// Screen-space UI: flat rectangles and text from the font atlas.

layout(location = 0) in vec2 aPos;   // framebuffer pixels, y down
layout(location = 1) in vec2 aUv;    // atlas uv; x < 0: solid colour
layout(location = 2) in vec4 aColor;

layout(location = 0) uniform vec2 uViewport;

out vec2 vUv;
out vec4 vColor;

void main() {
    vUv         = aUv;
    vColor      = aColor;
    gl_Position = vec4(aPos.x / uViewport.x * 2.0 - 1.0, 1.0 - aPos.y / uViewport.y * 2.0, 0.0, 1.0);
}
