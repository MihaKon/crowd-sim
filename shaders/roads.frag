#version 460
// Street surface from the pixel's position on the street. Pass 0: sidewalk over the whole
// quad; pass 1: carriageway with Japanese markings (drive on the left). Coordinates snap
// to 15 cm "pixels" to match the sprites; detail fades out when zooming out.

layout(location = 2) uniform float uPpm;
layout(location = 3) uniform int   uPass;

in vec2       vLocal;
in vec2       vWorld;
flat in uvec4 vInfo;
flat in float vLength;
flat in float vWidth;
out vec4      fragColor;

const float kSidewalk = 2.5;  // must match roads.cpp
const float kPx       = 0.15; // "pixel" size of the markings, metres

vec3 hex(uint c) { return vec3(float((c >> 16u) & 255u), float((c >> 8u) & 255u), float(c & 255u)) / 255.0; }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    uint  kind   = vInfo.x, flags = vInfo.y;
    float detail = smoothstep(2.0, 5.0, uPpm);  // px per metre: markings (>= 30 cm) readable from ~4 px/m
    float fine   = smoothstep(6.0, 12.0, uPpm); // paving seams only close up, else they read as noise
    vec3  farLocal = hex(0x3b4261u), farArt = hex(0x565f89u);

    float across, roadHalf;
    bool  art;
    if (kind == 2u) { // junction
        float r = length(vLocal);
        if (r > 1.0) discard;
        across   = r * 0.5 * vWidth;
        roadHalf = vLength;
        art      = roadHalf > 4.0;
    } else {
        art      = kind == 1u;
        roadHalf = art ? 8.0 : 3.5;
        across   = vLocal.y * 0.5 * vWidth;
    }
    float along = (floor(vLocal.x / kPx) + 0.5) * kPx;
    float qa    = (floor(across / kPx) + 0.5) * kPx;
    float av    = abs(qa);
    vec3  far   = art ? farArt : farLocal;

    if (uPass == 0) { // ---- sidewalk
        vec3 c;
        if (av < roadHalf + 0.25) {
            c = hex(0x5b6283u);
        } else {
            vec2  tile = kind == 2u ? floor(vWorld / 0.75) : vec2(floor(along / 0.75), floor((av - roadHalf) / 0.75));
            bool  seam = kind == 2u ? any(lessThan(fract(vWorld / 0.75), vec2(0.15)))
                                    : fract(along / 0.75) < 0.15 || fract((av - roadHalf) / 0.75) < 0.15;
            vec3 paving = hex(0x4b5170u) * (1.0 + fine * (0.06 * hash(tile) - 0.03));
            c = mix(paving, hex(0x434965u), seam ? fine : 0.0);
        }
        fragColor = vec4(mix(far, c, detail), 1.0);
        return;
    }

    // ---- carriageway
    if (av > roadHalf) discard;
    vec3 asphalt = (art ? hex(0x3a3f58u) : hex(0x34384eu)) * (0.96 + 0.08 * hash(floor(vWorld / 0.3)));
    vec3 c = asphalt;

    if (kind != 2u) {
        float trimA = float(vInfo.z) * 0.25, trimB = float(vInfo.w) * 0.25;
        float dA = along - trimA, dB = vLength - along - trimB; // past the junction mouth
        float white = 0.0, yellow = 0.0;
        if (dA > 0.0 && dB > 0.0) {
            bool zebraA = (flags & 1u) != 0u && dA > 0.6 && dA < 3.8;
            bool zebraB = (flags & 2u) != 0u && dB > 0.6 && dB < 3.8;
            if (zebraA || zebraB) {
                if (av < roadHalf - 0.3 && fract(qa / 1.0) < 0.5) white = 1.0; // 50 cm bars along the street
            } else if ((flags & 4u) != 0u && dA > 4.2 && dA < 4.65 && qa < 0.0) {
                white = 1.0; // stop line, incoming lane at the start (traffic from B keeps left)
            } else if ((flags & 8u) != 0u && dB > 4.2 && dB < 4.65 && qa > 0.0) {
                white = 1.0; // stop line, incoming lane at the end
            } else {
                float gA = (flags & 5u) != 0u ? 4.8 : 0.5, gB = (flags & 10u) != 0u ? 4.8 : 0.5;
                if (dA > gA && dB > gB) {
                    if (art) {
                        if (av > 0.15 && av < 0.45) yellow = 1.0;                            // centre line (both sides)
                        if (av > 3.85 && av < 4.15 && fract(along / 6.0) < 0.5) white = 1.0; // lane divider
                        if (av > 7.2 && av < 7.5) white = 1.0;                               // edge line
                    } else if (av > 2.9 && av < 3.2) {
                        white = 1.0; // side strip line
                    }
                }
            }
        }
        c = mix(c, hex(0xc0caf5u), white * 0.85);
        c = mix(c, hex(0xe0af68u), yellow * 0.9);
    }
    fragColor = vec4(mix(far, c, detail), 1.0);
}
