#version 460

layout(binding = 0) uniform sampler2D uAtlas; // R8 coverage

in vec2      vUv;
in vec4      vColor;
flat in vec4 vShape;
out vec4     fragColor;

void main() {
    float a;
    if (vShape.x > 0.0) { // rounded rectangle; a feather > 0 blurs it into a shadow
        vec2  q = abs(vUv) - vShape.xy + vShape.z;
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vShape.z;
        float f = max(vShape.w, 0.6);
        a = vShape.w > 0.0 ? 1.0 - smoothstep(-f, f, d) : clamp(0.5 - d, 0.0, 1.0);
    } else {
        a = vUv.x < 0.0 ? 1.0 : texture(uAtlas, vUv).r;
    }
    fragColor = vec4(vColor.rgb, vColor.a * a);
}
