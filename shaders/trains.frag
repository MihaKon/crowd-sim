#version 460
// Train body in the line colour; the bright part shows how full it is.

in float      vAlong;
flat in float vLoad;
flat in vec3  vColor;
out vec4      fragColor;

const vec3 kBackground = vec3(0.102, 0.106, 0.149); // #1a1b26

void main() {
    vec3 c    = vAlong <= vLoad ? vColor : mix(kBackground, vColor, 0.35);
    fragColor = vec4(c, 1.0);
}
