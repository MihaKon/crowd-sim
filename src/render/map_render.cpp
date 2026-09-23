#include "render/map_render.hpp"

#include "core/gl_util.hpp"
#include "core/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr int kTiles = 16;

enum Layer { kWaterL, kBlocksL, kRailL, kStationL, kLayerCount };

struct MapVertex {
    float    x, y;
    int8_t   ex, ey;
    uint8_t  minPx;
    uint8_t  pad;
    float    width;  // m, 0 = no extrusion
    uint32_t color;
};
static_assert(sizeof(MapVertex) == 20);

uint32_t rgba(uint32_t hex) {
    return ((hex >> 16) & 0xFFu) | (hex & 0xFF00u) | ((hex & 0xFFu) << 16) | 0xFF000000u;
}

struct MeshBuilder {
    std::vector<MapVertex> v;

    void vert(Vec2 p, float ex, float ey, float w, uint8_t minPx, uint32_t c) {
        v.push_back({p.x, p.y, int8_t(std::lround(ex * 127.0f)), int8_t(std::lround(ey * 127.0f)), minPx, 0, w, c});
    }

    void segment(Vec2 a, Vec2 b, float w, uint8_t minPx, uint32_t c) {
        const Vec2  d   = b - a;
        const float len = length(d);
        if (len < 1e-3f) return;
        const Vec2 n = perp(d / len);
        vert(a, n.x, n.y, w, minPx, c);
        vert(a, -n.x, -n.y, w, minPx, c);
        vert(b, n.x, n.y, w, minPx, c);
        vert(b, n.x, n.y, w, minPx, c);
        vert(a, -n.x, -n.y, w, minPx, c);
        vert(b, -n.x, -n.y, w, minPx, c);
    }

    void square(Vec2 p, float w, uint8_t minPx, uint32_t c) {
        vert(p, -1, -1, w, minPx, c);
        vert(p, 1, -1, w, minPx, c);
        vert(p, 1, 1, w, minPx, c);
        vert(p, -1, -1, w, minPx, c);
        vert(p, 1, 1, w, minPx, c);
        vert(p, -1, 1, w, minPx, c);
    }

    void triangle(Vec2 a, Vec2 b, Vec2 c, uint32_t col) {
        vert(a, 0, 0, 0, 0, col);
        vert(b, 0, 0, 0, 0, col);
        vert(c, 0, 0, 0, 0, col);
    }
};

struct Bins {
    float                    size;
    std::vector<MeshBuilder> bins = std::vector<MeshBuilder>(size_t(kLayerCount) * kTiles * kTiles);

    MeshBuilder& at(int layer, Vec2 p) {
        const int tx = std::clamp(int(p.x / size * kTiles), 0, kTiles - 1);
        const int ty = std::clamp(int(p.y / size * kTiles), 0, kTiles - 1);
        return bins[(size_t(layer) * kTiles + size_t(ty)) * kTiles + size_t(tx)];
    }
};

uint32_t blockColor(BlockType t) {
    switch (t) {
    case BlockType::Residential: return rgba(palette::kResidential);
    case BlockType::Commercial:  return rgba(palette::kCommercial);
    case BlockType::Office:      return rgba(palette::kOffice);
    case BlockType::Industrial:  return rgba(palette::kIndustrial);
    case BlockType::Park:        return rgba(palette::kPark);
    case BlockType::ResidentialPoor: return rgba(palette::kResidentialPoor);
    case BlockType::ResidentialRich: return rgba(palette::kResidentialRich);
    }
    return 0;
}

void buildBins(const CityMap& m, Bins& b) {
    const Vec2 centre{m.size * 0.5f, m.size * 0.5f};

    for (size_t i = 1; i + 1 < m.water.size(); ++i)
        b.at(kWaterL, centre).triangle(m.water[0], m.water[i], m.water[i + 1], rgba(palette::kWater));

    // Blocks as fans from the centroid: they are near-convex after the inset.
    for (const Block& blk : m.blocks) {
        MeshBuilder& mb = b.at(kBlocksL, blk.centroid);
        for (size_t i = 0; i < blk.poly.size(); ++i)
            mb.triangle(blk.centroid, blk.poly[i], blk.poly[(i + 1) % blk.poly.size()], blockColor(blk.type));
    }

    for (size_t li = 0; li < m.lines.size(); ++li) {
        const RailLine& l = m.lines[li];
        const uint32_t  c = rgba(palette::kRail[li % palette::kRailCount]);
        const size_t    n = l.path.size();
        for (size_t i = 0; i + 1 < n + (l.loop ? 1 : 0); ++i) {
            const Vec2 p = l.path[i], q = l.path[(i + 1) % n];
            b.at(kRailL, (p + q) * 0.5f).segment(p, q, 10.0f, 2, c);
        }
        for (const Vec2 p : l.path) b.at(kRailL, p).square(p, 10.0f, 2, c);
    }

    // Station outline and fill share a bin, so the fill is drawn after the outline.
    for (const Station& s : m.stations) {
        MeshBuilder& mb = b.at(kStationL, s.pos);
        mb.square(s.pos, 90.0f, 8, rgba(palette::kBackground));
        mb.square(s.pos, 60.0f, 6, rgba(palette::kStation));
    }
}

enum Loc : GLint { kCenter = 0, kScale, kPpm };

} // namespace

void MapRenderer::init(const std::string& shaderDir) {
    prog_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/map.vert"),
                             gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/map.frag")});
    glCreateVertexArrays(1, &vao_);
    glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, offsetof(MapVertex, x));
    glVertexArrayAttribFormat(vao_, 1, 2, GL_BYTE, GL_TRUE, offsetof(MapVertex, ex));
    glVertexArrayAttribFormat(vao_, 2, 1, GL_UNSIGNED_BYTE, GL_FALSE, offsetof(MapVertex, minPx));
    glVertexArrayAttribFormat(vao_, 3, 1, GL_FLOAT, GL_FALSE, offsetof(MapVertex, width));
    glVertexArrayAttribFormat(vao_, 4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(MapVertex, color));
    for (GLuint i = 0; i < 5; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
}

void MapRenderer::upload(const CityMap& map) {
    Bins bins{map.size};
    buildBins(map, bins);

    std::vector<MapVertex> mesh;
    ranges_.clear();
    for (size_t bi = 0; bi < bins.bins.size(); ++bi) {
        const MeshBuilder& mb = bins.bins[bi];
        if (mb.v.empty()) continue;
        const int layer = int(bi / (size_t(kTiles) * kTiles));
        Range     r{GLint(mesh.size()), GLsizei(mb.v.size()), 1e30f, 1e30f, -1e30f, -1e30f, layer};
        for (const MapVertex& v : mb.v) {
            r.minX = std::min(r.minX, v.x);
            r.minY = std::min(r.minY, v.y);
            r.maxX = std::max(r.maxX, v.x);
            r.maxY = std::max(r.maxY, v.y);
        }
        ranges_.push_back(r);
        mesh.insert(mesh.end(), mb.v.begin(), mb.v.end());
    }
    total_ = mesh.size();

    if (vbo_) glDeleteBuffers(1, &vbo_);
    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, GLsizeiptr(std::max<size_t>(1, mesh.size()) * sizeof(MapVertex)), mesh.data(), 0);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(MapVertex));
}

void MapRenderer::draw(const Camera& cam, int fbW, int fbH, Layers which) const {
    const int lo = which == kGround ? kWaterL : kRailL, hi = which == kGround ? kBlocksL : kStationL;
    // Range bounds are taken before extrusion: grow the view by the widest element.
    const float margin = 45.0f + 5.0f / cam.ppm;
    const float hw = 0.5f * float(fbW) / cam.ppm + margin, hh = 0.5f * float(fbH) / cam.ppm + margin;
    const float x0 = cam.cx - hw, x1 = cam.cx + hw, y0 = cam.cy - hh, y1 = cam.cy + hh;

    firsts_.clear();
    counts_.clear();
    for (const Range& r : ranges_)
        if (r.layer >= lo && r.layer <= hi && r.maxX >= x0 && r.minX <= x1 && r.maxY >= y0 && r.minY <= y1) {
            firsts_.push_back(r.first);
            counts_.push_back(r.count);
        }
    if (firsts_.empty()) return;

    glUseProgram(prog_);
    glProgramUniform2f(prog_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(prog_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(prog_, kPpm, cam.ppm);
    glBindVertexArray(vao_);
    glMultiDrawArrays(GL_TRIANGLES, firsts_.data(), counts_.data(), GLsizei(firsts_.size()));
}

void MapRenderer::destroy() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(prog_);
    vbo_ = vao_ = prog_ = 0;
}
