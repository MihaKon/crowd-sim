#version 460

layout(binding = 1) uniform sampler2D uCars; // SpriteAtlas car sprites

in vec2       vUv;
flat in float vBraking;
out vec4 fragColor;

void main() {
    vec4 c = texture(uCars, vUv);
    if (c.a < 0.35) discard;
    // Tail lights are flagged with alpha 250 (only exact at full size; minified
    // sprites are too small to show them anyway): bright while braking / standing.
    if (abs(c.a - 250.0 / 255.0) < 0.002) c.rgb = vBraking > 0.5 ? vec3(1.0, 0.25, 0.25) : vec3(0.55, 0.16, 0.16);
    fragColor = vec4(c.rgb / max(c.a, 1e-3), 1.0); // mipmapped edges: un-premultiply the averaged transparent black
}
