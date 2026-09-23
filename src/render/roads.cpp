#include "render/roads.hpp"

#include "core/gl_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

constexpr float kSidewalk = 2.5f; // m, must match roads.frag

struct RoadVertex {
    float   x, y;
    float   along;     // metres from the street's start node (streets), 0 (junctions)
    float   length;    // street length (streets), carriageway radius (junctions)
    float   width;
    int8_t  ex, ey;
    uint8_t kind;      // 0 local street, 1 arterial street, 2 junction
    uint8_t flags;     // bit 0/1 zebra at start/end, bit 2/3 stop line at start/end
    uint8_t trimA, trimB; // junction radius at start / end, 0.25 m units
    uint8_t minPx;
    uint8_t side;      // streets: 1 left edge, 0 right edge
};
static_assert(sizeof(RoadVertex) == 28);

int8_t snorm(float v) { return int8_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f)); }
uint8_t quarterMetres(float m) { return uint8_t(std::clamp(std::lround(m * 4.0f), 0L, 255L)); }

std::vector<RoadVertex> buildMesh(const CityMap& m) {
    const size_t n = m.nodes.size();
    std::vector<float>   radius(n, 0.0f);
    std::vector<uint8_t> arterialDeg(n, 0);
    for (const Edge& e : m.edges) {
        const float half = 0.5f * roadWidth(e.type);
        radius[e.a]      = std::max(radius[e.a], half);
        radius[e.b]      = std::max(radius[e.b], half);
        if (e.type == RoadType::Arterial) {
            ++arterialDeg[e.a];
            ++arterialDeg[e.b];
        }
    }

    std::vector<RoadVertex> v;
    v.reserve(m.edges.size() * 6 + n * 6);

    for (RoadType pass : {RoadType::Local, RoadType::Arterial}) {
        for (const Edge& e : m.edges) {
            if (e.type != pass) continue;
            const Vec2  a = m.nodes[e.a], b = m.nodes[e.b], d = b - a;
            const float len = length(d);
            if (len < 1e-3f) continue;
            const Vec2 nrm = perp(d / len);
            const bool art = e.type == RoadType::Arterial;

            // Zebra and stop line where arterials cross and where a side street meets an arterial.
            auto crossing = [&](uint32_t node) { return art ? arterialDeg[node] >= 3 : arterialDeg[node] >= 1; };
            uint8_t flags = 0;
            if (crossing(e.a)) flags |= 1 | 4;
            if (crossing(e.b)) flags |= 2 | 8;

            const float   width = roadWidth(e.type) + 2.0f * kSidewalk;
            const uint8_t ta = quarterMetres(radius[e.a]), tb = quarterMetres(radius[e.b]);
            const uint8_t kind = art ? 1 : 0, minPx = art ? 2 : 1;
            auto vert = [&](Vec2 p, float along, float s) {
                v.push_back({p.x, p.y, along, len, width, snorm(nrm.x * s), snorm(nrm.y * s), kind, flags, ta, tb,
                             minPx, uint8_t(s > 0.0f ? 1 : 0)});
            };
            vert(a, 0.0f, 1.0f);
            vert(a, 0.0f, -1.0f);
            vert(b, len, 1.0f);
            vert(b, len, 1.0f);
            vert(a, 0.0f, -1.0f);
            vert(b, len, -1.0f);
        }
    }

    for (size_t i = 0; i < n; ++i) {
        if (radius[i] <= 0.0f) continue;
        const Vec2    p     = m.nodes[i];
        const float   width = 2.0f * (radius[i] + kSidewalk);
        const uint8_t minPx = arterialDeg[i] ? 2 : 1;
        auto vert = [&](float cx, float cy) {
            v.push_back({p.x, p.y, 0.0f, radius[i], width, snorm(cx), snorm(cy), 2, 0, 0, 0, minPx, 0});
        };
        vert(-1, -1);
        vert(1, -1);
        vert(1, 1);
        vert(-1, -1);
        vert(1, 1);
        vert(-1, 1);
    }
    return v;
}

enum Loc : GLint { kCenter = 0, kScale, kPpm, kPass };

} // namespace

void RoadRenderer::init(const std::string& shaderDir) {
    prog_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/roads.vert"),
                             gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/roads.frag")});
    glCreateVertexArrays(1, &vao_);
    glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, offsetof(RoadVertex, x));
    glVertexArrayAttribFormat(vao_, 1, 1, GL_FLOAT, GL_FALSE, offsetof(RoadVertex, along));
    glVertexArrayAttribFormat(vao_, 2, 1, GL_FLOAT, GL_FALSE, offsetof(RoadVertex, length));
    glVertexArrayAttribFormat(vao_, 3, 1, GL_FLOAT, GL_FALSE, offsetof(RoadVertex, width));
    glVertexArrayAttribFormat(vao_, 4, 2, GL_BYTE, GL_TRUE, offsetof(RoadVertex, ex));
    glVertexArrayAttribIFormat(vao_, 5, 4, GL_UNSIGNED_BYTE, offsetof(RoadVertex, kind));
    glVertexArrayAttribIFormat(vao_, 6, 2, GL_UNSIGNED_BYTE, offsetof(RoadVertex, minPx));
    for (GLuint i = 0; i < 7; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
}

void RoadRenderer::upload(const CityMap& map) {
    const std::vector<RoadVertex> mesh = buildMesh(map);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, GLsizeiptr(std::max<size_t>(1, mesh.size()) * sizeof(RoadVertex)), mesh.data(), 0);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(RoadVertex));
    count_ = GLsizei(mesh.size());
}

void RoadRenderer::draw(const Camera& cam, int fbW, int fbH) const {
    glUseProgram(prog_);
    glProgramUniform2f(prog_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(prog_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(prog_, kPpm, cam.ppm);
    glBindVertexArray(vao_);
    for (int pass = 0; pass < 2; ++pass) {
        glProgramUniform1i(prog_, kPass, pass);
        glDrawArrays(GL_TRIANGLES, 0, count_);
    }
}

void RoadRenderer::destroy() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(prog_);
    vbo_ = vao_ = prog_ = 0;
}
