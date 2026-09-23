#version 460

layout(binding = 0) uniform sampler2D uAtlas;

flat in vec4 vUv;
out vec4 fragColor;

void main() {
    vec2  pc = gl_PointCoord; // 0..1 within the point quad, y down like the atlas
    float u  = mix(vUv.x, vUv.z, pc.x);
    float v  = mix(vUv.y, vUv.w, pc.y);
    vec4  c = texture(uAtlas, vec2(u, v));
    if (c.a < 0.35) discard;
    fragColor = vec4(c.rgb / max(c.a, 1e-3), 1.0); // see cars.frag
}
