#version 460

layout(binding = 0) uniform sampler2D uAtlas; // R8 coverage

in vec2  vUv;
in vec4  vColor;
out vec4 fragColor;

void main() {
    float a   = vUv.x < 0.0 ? 1.0 : texture(uAtlas, vUv).r;
    fragColor = vec4(vColor.rgb, vColor.a * a);
}
