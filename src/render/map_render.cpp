#include "render/map_render.hpp"

#include "core/gl_util.hpp"
#include "core/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr int kTiles = 16;

enum Layer { kLandL, kWaterL, kShoreL, kBlocksL, kRailL, kPlatformL, kStationL, kLayerCount };

// Surface materials, shaded procedurally in map.frag (must match).
enum Material : uint8_t {
    kMatFlat = 0, kMatLand, kMatBlock, kMatWater, kMatShore, kMatRail, kMatPlatform, kMatStation
};

struct MapVertex {
    float    x, y;
    int8_t   ex, ey;
    uint8_t  minPx;
    uint8_t  material;
    float    width;  // m, 0 = no extrusion
    uint32_t color;  // kMatFlat, rail: sRGB colour (stations: line count in alpha); kMatBlock: BlockType
    float    param;  // blocks: 0 centre .. 1 edge; shore: -1 open water .. 0 waterline .. 1 land;
                     // rail: -1..1 across the bed; platform, station: local x, -1..1
    float    param2; // rail: metres along the line; platform, station: local y, -1..1
};
static_assert(sizeof(MapVertex) == 28);

uint32_t rgba(uint32_t hex) {
    return ((hex >> 16) & 0xFFu) | (hex & 0xFF00u) | ((hex & 0xFFu) << 16) | 0xFF000000u;
}

struct MeshBuilder {
    std::vector<MapVertex> v;

    void vert(Vec2 p, float ex, float ey, float w, uint8_t minPx, uint32_t c, uint8_t mat = kMatFlat,
              float param = 0.0f, float param2 = 0.0f) {
        v.push_back({p.x, p.y, int8_t(std::lround(ex * 127.0f)), int8_t(std::lround(ey * 127.0f)), minPx, mat, w, c,
                     param, param2});
    }

    // A quad along a -> b, extruded sideways by width / 2 (at least minPx / 2 pixels):
    // param runs -1..1 across, param2 from along0 to along0 + |b - a|.
    void strip(Vec2 a, Vec2 b, float w, uint8_t minPx, uint32_t c, uint8_t mat, float along0) {
        const Vec2  d   = b - a;
        const float len = length(d);
        if (len < 1e-3f) return;
        const Vec2  n = perp(d / len);
        const float a1 = along0 + len;
        vert(a, n.x, n.y, w, minPx, c, mat, 1.0f, along0);
        vert(a, -n.x, -n.y, w, minPx, c, mat, -1.0f, along0);
        vert(b, n.x, n.y, w, minPx, c, mat, 1.0f, a1);
        vert(b, n.x, n.y, w, minPx, c, mat, 1.0f, a1);
        vert(a, -n.x, -n.y, w, minPx, c, mat, -1.0f, along0);
        vert(b, -n.x, -n.y, w, minPx, c, mat, -1.0f, a1);
    }

    // An oriented rectangle with local coordinates -1..1 in param / param2.
    void rect(Vec2 centre, Vec2 ax, float hx, float hy, uint32_t c, uint8_t mat) {
        const Vec2 ay = perp(ax);
        auto corner = [&](float u, float v) { vert(centre + ax * (u * hx) + ay * (v * hy), 0, 0, 0, 0, c, mat, u, v); };
        corner(-1, -1);
        corner(1, -1);
        corner(1, 1);
        corner(-1, -1);
        corner(1, 1);
        corner(-1, 1);
    }

    // A square around p, min(w metres, minPx pixels) wide; local coordinates -1..1.
    void square(Vec2 p, float w, uint8_t minPx, uint32_t c, uint8_t mat) {
        for (Vec2 q : {Vec2{-1, -1}, Vec2{1, -1}, Vec2{1, 1}, Vec2{-1, -1}, Vec2{1, 1}, Vec2{-1, 1}})
            vert(p, q.x, q.y, w, minPx, c, mat, q.x, q.y);
    }

    void triangle(Vec2 a, Vec2 b, Vec2 c, uint32_t col, uint8_t mat, float pa, float pb, float pc) {
        vert(a, 0, 0, 0, 0, col, mat, pa);
        vert(b, 0, 0, 0, 0, col, mat, pb);
        vert(c, 0, 0, 0, 0, col, mat, pc);
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

void buildBins(const CityMap& m, Bins& b) {
    // Countryside under everything, reaching well past the map, where it fades into haze.
    {
        const float lo = -0.6f * m.size, hi = 1.6f * m.size;
        const Vec2  c{m.size * 0.5f, m.size * 0.5f};
        MeshBuilder& mb = b.at(kLandL, c);
        mb.triangle({lo, lo}, {hi, lo}, {hi, hi}, 0, kMatLand, 0, 0, 0);
        mb.triangle({lo, lo}, {hi, hi}, {lo, hi}, 0, kMatLand, 0, 0, 0);
    }

    if (m.water.size() > 2) {
        const Vec2 c = m.water[0];
        for (size_t i = 1; i + 1 < m.water.size(); ++i)
            b.at(kWaterL, c).triangle(c, m.water[i], m.water[i + 1], 0, kMatWater, 0.0f, 0.0f, 0.0f);
        // Shore: a narrow strip across the waterline, foam out to sea, quay and sand
        // inland. Rows at the sea edge (-1), the waterline (0) and inland (+1). The
        // wider shallows come from the distance field (buildShoreField).
        constexpr float kSea = 24.0f, kLand = 18.0f;
        for (size_t i = 1; i + 1 < m.water.size(); ++i) {
            const Vec2 p = m.water[i], q = m.water[i + 1];
            const Vec2 d = q - p;
            if (length2(d) < 1e-2f) continue;
            Vec2 n = normalize(perp(d));
            if (dot(n, (p + q) * 0.5f - c) < 0.0f) n = -n; // outward, towards the land
            MeshBuilder& mb = b.at(kShoreL, p);
            auto quad = [&](float a0, float a1, float o0, float o1) {
                const Vec2 p0 = p + n * o0, q0 = q + n * o0, p1 = p + n * o1, q1 = q + n * o1;
                mb.triangle(p0, p1, q1, 0, kMatShore, a0, a1, a1);
                mb.triangle(p0, q1, q0, 0, kMatShore, a0, a1, a0);
            };
            quad(-1.0f, 0.0f, -kSea, 0.0f);
            quad(0.0f, 1.0f, 0.0f, kLand);
        }
    }
    // The palace moat: a ring of water around the clearing in the park.
    if (m.palaceRadius > 0.0f) {
        const float r0 = 0.25f * m.palaceRadius, r1 = r0 + 14.0f;
        for (int i = 0; i < 96; ++i) {
            const float a0 = 6.2831853f * float(i) / 96.0f, a1 = 6.2831853f * float(i + 1) / 96.0f;
            const Vec2  d0{std::cos(a0), std::sin(a0)}, d1{std::cos(a1), std::sin(a1)};
            MeshBuilder& mb = b.at(kShoreL, m.palace);
            mb.triangle(m.palace + d0 * r0, m.palace + d0 * r1, m.palace + d1 * r1, 0, kMatWater, 0, 0, 0);
            mb.triangle(m.palace + d0 * r0, m.palace + d1 * r1, m.palace + d1 * r0, 0, kMatWater, 0, 0, 0);
        }
    }

    // Blocks as fans from the centroid (near-convex after the inset); the parameter
    // runs 0 at the centroid to 1 on the edge, for the curb in the shader.
    for (const Block& blk : m.blocks) {
        MeshBuilder& mb = b.at(kBlocksL, blk.centroid);
        for (size_t i = 0; i < blk.poly.size(); ++i)
            mb.triangle(blk.centroid, blk.poly[i], blk.poly[(i + 1) % blk.poly.size()], uint32_t(blk.type), kMatBlock,
                        0.0f, 1.0f, 1.0f);
    }

    // Track beds along each line; the parameter along is the distance from its start,
    // for the sleepers. Segments overlap by a metre so joints have no gaps.
    for (size_t li = 0; li < m.lines.size(); ++li) {
        const RailLine& l = m.lines[li];
        const uint32_t  c = rgba(palette::kRail[li % palette::kRailCount]);
        const size_t    n = l.path.size();
        float           along = 0.0f;
        for (size_t i = 0; i + 1 < n + (l.loop ? 1 : 0); ++i) {
            const Vec2  p = l.path[i], q = l.path[(i + 1) % n];
            const float len = length(q - p);
            if (len < 1e-3f) continue;
            const Vec2 d = (q - p) / len;
            // Bed ±4.6 m, at least 4 px wide.
            b.at(kRailL, (p + q) * 0.5f).strip(p - d * 0.5f, q + d * 0.5f, 9.2f, 4, c, kMatRail, along - 0.5f);
            along += len;
        }
    }
    // Platforms: two side platforms along the tracks at every stop, under canopies.
    for (const RailStop& st : railStops(m)) {
        const uint32_t c = rgba(palette::kRail[st.line % palette::kRailCount]);
        const Vec2     side = perp(st.dir);
        const float    off  = 0.5f * kTrackGap + 1.7f + 2.2f;
        for (float sgn : {-1.0f, 1.0f})
            b.at(kPlatformL, st.pos).rect(st.pos + side * (off * sgn), st.dir, 0.5f * kPlatformLength, 2.2f * sgn, c,
                                          kMatPlatform);
    }
    // Station squares: a plaza close up, a map symbol (at least 14 px) far away.
    for (const Station& s : m.stations) {
        const uint32_t line = s.lines.empty() ? 0 : s.lines[0] % palette::kRailCount;
        const uint32_t lines = uint32_t(std::min<size_t>(s.lines.size(), 255));
        const uint32_t c     = (rgba(palette::kRail[line]) & 0x00FFFFFFu) | (lines << 24); // line count in alpha
        b.at(kStationL, s.pos).square(s.pos, kPlazaSize, 14, c, kMatStation);
    }
}

// Signed distance to the waterline (metres, negative in the water) on a grid over
// the map and its surroundings, for smooth shallows. Water is the fan of m.water.
constexpr int kField = 1024;

std::vector<float> buildShoreField(const CityMap& m, float& origin, float& extent) {
    origin = -0.3f * m.size;
    extent = 1.6f * m.size;
    const float cellM = extent / float(kField);
    std::vector<float> d(size_t(kField) * kField, 1e9f);
    if (m.water.size() < 3) return d;

    const Vec2   c  = m.water[0];
    const size_t nr = m.water.size() - 1; // rim points, evenly spaced angles, first == last
    std::vector<uint8_t> wet(d.size());
    for (int y = 0; y < kField; ++y)
        for (int x = 0; x < kField; ++x) {
            const Vec2  p{origin + (float(x) + 0.5f) * cellM, origin + (float(y) + 0.5f) * cellM};
            const Vec2  v = p - c;
            float       a = std::atan2(v.y, v.x);
            if (a < 0.0f) a += 6.2831853f;
            const float f  = a / 6.2831853f * float(nr - 1);
            const size_t i = std::min(size_t(f), nr - 2);
            const float r  = lerpf(length(m.water[1 + i] - c), length(m.water[2 + i] - c), f - float(i));
            wet[size_t(y) * kField + size_t(x)] = length(v) < r;
        }
    // Two-pass chamfer distance (1, sqrt 2) from the cells on the other side of the line.
    for (size_t i = 0; i < d.size(); ++i) {
        const int x = int(i % kField), y = int(i / kField);
        for (int k = 0; k < 4; ++k) {
            const int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
            if (nx >= 0 && ny >= 0 && nx < kField && ny < kField && wet[size_t(ny) * kField + size_t(nx)] != wet[i])
                d[i] = 0.5f;
        }
    }
    const float diag = 1.41421356f;
    for (int y = 0; y < kField; ++y)
        for (int x = 0; x < kField; ++x) {
            float& v = d[size_t(y) * kField + size_t(x)];
            if (x > 0) v = std::min(v, d[size_t(y) * kField + size_t(x - 1)] + 1.0f);
            if (y > 0) {
                v = std::min(v, d[size_t(y - 1) * kField + size_t(x)] + 1.0f);
                if (x > 0) v = std::min(v, d[size_t(y - 1) * kField + size_t(x - 1)] + diag);
                if (x + 1 < kField) v = std::min(v, d[size_t(y - 1) * kField + size_t(x + 1)] + diag);
            }
        }
    for (int y = kField - 1; y >= 0; --y)
        for (int x = kField - 1; x >= 0; --x) {
            float& v = d[size_t(y) * kField + size_t(x)];
            if (x + 1 < kField) v = std::min(v, d[size_t(y) * kField + size_t(x + 1)] + 1.0f);
            if (y + 1 < kField) {
                v = std::min(v, d[size_t(y + 1) * kField + size_t(x)] + 1.0f);
                if (x + 1 < kField) v = std::min(v, d[size_t(y + 1) * kField + size_t(x + 1)] + diag);
                if (x > 0) v = std::min(v, d[size_t(y + 1) * kField + size_t(x - 1)] + diag);
            }
        }
    for (size_t i = 0; i < d.size(); ++i) d[i] = (wet[i] ? -d[i] : d[i]) * cellM;
    return d;
}

enum Loc : GLint { kCenter = 0, kScale, kPpm, kSize, kField0 };

} // namespace

void MapRenderer::init(const std::string& shaderDir) {
    prog_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/map.vert"),
                             gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/map.frag")});
    glCreateVertexArrays(1, &vao_);
    glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, offsetof(MapVertex, x));
    glVertexArrayAttribFormat(vao_, 1, 2, GL_BYTE, GL_TRUE, offsetof(MapVertex, ex));
    glVertexArrayAttribIFormat(vao_, 2, 2, GL_UNSIGNED_BYTE, offsetof(MapVertex, minPx)); // minPx, material
    glVertexArrayAttribFormat(vao_, 3, 1, GL_FLOAT, GL_FALSE, offsetof(MapVertex, width));
    glVertexArrayAttribIFormat(vao_, 4, 1, GL_UNSIGNED_INT, offsetof(MapVertex, color));
    glVertexArrayAttribFormat(vao_, 5, 2, GL_FLOAT, GL_FALSE, offsetof(MapVertex, param));
    for (GLuint i = 0; i < 6; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
}

void MapRenderer::upload(const CityMap& map) {
    size_ = map.size;
    {
        const std::vector<float> field = buildShoreField(map, fieldOrigin_, fieldExtent_);
        if (!fieldTex_) {
            glCreateTextures(GL_TEXTURE_2D, 1, &fieldTex_);
            glTextureStorage2D(fieldTex_, 1, GL_R16F, kField, kField);
            glTextureParameteri(fieldTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(fieldTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(fieldTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(fieldTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        std::vector<float> clamped(field.size());
        for (size_t i = 0; i < field.size(); ++i) clamped[i] = std::clamp(field[i], -60000.0f, 60000.0f);
        glTextureSubImage2D(fieldTex_, 0, 0, 0, kField, kField, GL_RED, GL_FLOAT, clamped.data());
    }
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

void MapRenderer::drawLayers(const Camera& cam, int fbW, int fbH, int lo, int hi) const {
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
    if (!firsts_.empty()) glMultiDrawArrays(GL_TRIANGLES, firsts_.data(), counts_.data(), GLsizei(firsts_.size()));
}

void MapRenderer::draw(const Camera& cam, int fbW, int fbH, Layers which) const {
    glUseProgram(prog_);
    glProgramUniform2f(prog_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(prog_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(prog_, kPpm, cam.ppm);
    glProgramUniform1f(prog_, kSize, size_);
    glProgramUniform2f(prog_, kField0, fieldOrigin_, 1.0f / fieldExtent_);
    glBindTextureUnit(3, fieldTex_);
    glBindVertexArray(vao_);
    if (which == kOverlay) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawLayers(cam, fbW, fbH, kRailL, kStationL);
        glDisable(GL_BLEND);
        return;
    }
    // Water and blocks first, writing depth; the countryside then only shades what
    // they left uncovered (early depth test); the shore blends over both.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    drawLayers(cam, fbW, fbH, kWaterL, kWaterL);
    drawLayers(cam, fbW, fbH, kBlocksL, kBlocksL);
    drawLayers(cam, fbW, fbH, kLandL, kLandL);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawLayers(cam, fbW, fbH, kShoreL, kShoreL);
    glDisable(GL_BLEND);
}

void MapRenderer::destroy() {
    glDeleteTextures(1, &fieldTex_);
    fieldTex_ = 0;
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(prog_);
    vbo_ = vao_ = prog_ = 0;
}
