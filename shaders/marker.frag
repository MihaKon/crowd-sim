#version 460

in vec2  vLocal;
out vec4 fragColor;

void main() {
    float r = length(vLocal);
    if (r > 1.0 || r < 0.72) discard;
    fragColor = vec4(0.878, 0.686, 0.408, 1.0); // #e0af68
}
