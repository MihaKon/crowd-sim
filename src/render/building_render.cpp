#include "render/building_render.hpp"

#include "core/gl_util.hpp"

#include <algorithm>

namespace {

constexpr int   kTiles       = 16;
constexpr float kMinZoomPpm  = 1.0f;
constexpr int   kVerts       = 78;   // must match buildings.vert: 4 walls + 9 roof cells, 6 each
constexpr int   kShadowVerts = 36;   // must match building_shadow.vert
constexpr float kLean        = 0.5f; // roof lift per metre of height, must match the shaders
constexpr float kFloorHeight = 3.0f;

// std430 layout, must match buildings.vert / building_shadow.vert
struct Gpu {
    float    ox, oy;
    float    ax, ay;  // axisAlong (unit); axisIn = perp(axisAlong) * (bit 3 ? -1 : 1)
    float    sizeAlong, sizeIn;
    uint32_t info;    // bits 0-2 roof set, bit 3 flip, bits 4-6 facade, bits 7-8 windows, bits 9-31 seed
    float    height;
};
static_assert(sizeof(Gpu) == 32);

enum Loc : GLint { kCenter = 0, kScale, kPpm, kNight };

} // namespace

void BuildingRenderer::init(const std::string& shaderDir) {
    prog_       = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/buildings.vert"),
                                   gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/buildings.frag")});
    shadowProg_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/building_shadow.vert"),
                                   gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/building_shadow.frag")});
    glCreateVertexArrays(1, &vao_);
}

void BuildingRenderer::upload(const CityMap& map) {
    struct Item {
        Gpu   g;
        float sortY;
    };
    std::vector<std::vector<Item>> bins(size_t(kTiles) * kTiles);
    uint32_t                       seed = 0x9E3779B9u;
    for (const Building& b : map.buildings) {
        const float along = float(b.tilesAlong) * kBuildingTileMetres, in = float(b.tilesIn) * kBuildingTileMetres;
        const Vec2  c     = b.origin + b.axisAlong * (0.5f * along) + b.axisIn * (0.5f * in);
        const int   tx    = std::clamp(int(c.x / map.size * kTiles), 0, kTiles - 1);
        const int   ty    = std::clamp(int(c.y / map.size * kTiles), 0, kTiles - 1);
        const bool  flip  = dot(perp(b.axisAlong), b.axisIn) < 0.0f;
        seed              = seed * 1664525u + 1013904223u;
        const Gpu g{b.origin.x, b.origin.y, b.axisAlong.x, b.axisAlong.y, along, in,
                    uint32_t(b.palette & 7u) | (flip ? 8u : 0u) | (uint32_t(b.facade & 7u) << 4) |
                        (uint32_t(b.windows & 3u) << 7) | (seed & 0xFFFFFE00u),
                    float(b.floors) * kFloorHeight};
        bins[size_t(ty) * kTiles + size_t(tx)].push_back({g, c.y});
    }

    // Bins north to south (painter's order across rows), each sorted north to south.
    std::vector<Gpu> all;
    ranges_.clear();
    for (int ty = kTiles - 1; ty >= 0; --ty)
        for (int tx = 0; tx < kTiles; ++tx) {
            auto& bin = bins[size_t(ty) * kTiles + size_t(tx)];
            if (bin.empty()) continue;
            std::sort(bin.begin(), bin.end(), [](const Item& a, const Item& b) { return a.sortY > b.sortY; });
            Range r{GLint(all.size()), GLsizei(bin.size()), 1e30f, 1e30f, -1e30f, -1e30f};
            for (const Item& it : bin) {
                const Gpu& g = it.g;
                const Vec2 o{g.ox, g.oy}, a{g.ax, g.ay};
                const Vec2 in = perp(a) * ((g.info & 8u) ? -1.0f : 1.0f);
                // Footprint, lifted roof (+y) and shadow (+x, -y) all count for culling.
                const float lift = g.height * kLean;
                for (Vec2 p : {o, o + a * g.sizeAlong, o + in * g.sizeIn, o + a * g.sizeAlong + in * g.sizeIn}) {
                    r.minX = std::min(r.minX, p.x);
                    r.minY = std::min(r.minY, p.y - g.height * 0.35f);
                    r.maxX = std::max(r.maxX, p.x + g.height * 0.55f);
                    r.maxY = std::max(r.maxY, p.y + lift);
                }
                all.push_back(g);
            }
            ranges_.push_back(r);
        }
    count_ = all.size();

    if (ssbo_) glDeleteBuffers(1, &ssbo_);
    glCreateBuffers(1, &ssbo_);
    glNamedBufferStorage(ssbo_, GLsizeiptr(std::max<size_t>(1, all.size()) * sizeof(Gpu)), all.data(), 0);
}

bool BuildingRenderer::collect(const Camera& cam, int fbW, int fbH, int vertsPerBuilding) const {
    if (cam.ppm < kMinZoomPpm || ranges_.empty()) return false;
    const float hw = 0.5f * float(fbW) / cam.ppm, hh = 0.5f * float(fbH) / cam.ppm;
    const float x0 = cam.cx - hw, x1 = cam.cx + hw, y0 = cam.cy - hh, y1 = cam.cy + hh;
    firsts_.clear();
    counts_.clear();
    for (const Range& r : ranges_)
        if (r.maxX >= x0 && r.minX <= x1 && r.maxY >= y0 && r.minY <= y1) {
            firsts_.push_back(r.firstBuilding * vertsPerBuilding);
            counts_.push_back(r.buildings * vertsPerBuilding);
        }
    return !firsts_.empty();
}

void BuildingRenderer::drawShadows(const Camera& cam, int fbW, int fbH) const {
    if (!collect(cam, fbW, fbH, kShadowVerts)) return;
    glUseProgram(shadowProg_);
    glProgramUniform2f(shadowProg_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(shadowProg_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 19, ssbo_);
    glBindVertexArray(vao_);

    // Each pixel darkened once: pass only where the stencil is still 0, then mark it.
    glClear(GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMultiDrawArrays(GL_TRIANGLES, firsts_.data(), counts_.data(), GLsizei(firsts_.size()));
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
}

void BuildingRenderer::draw(const Camera& cam, int fbW, int fbH, float night) const {
    if (!collect(cam, fbW, fbH, kVerts)) return;
    glUseProgram(prog_);
    glProgramUniform2f(prog_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(prog_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(prog_, kPpm, cam.ppm);
    glProgramUniform1f(prog_, kNight, night);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 19, ssbo_);
    glBindVertexArray(vao_);
    glMultiDrawArrays(GL_TRIANGLES, firsts_.data(), counts_.data(), GLsizei(firsts_.size()));
}

void BuildingRenderer::destroy() {
    glDeleteBuffers(1, &ssbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(prog_);
    glDeleteProgram(shadowProg_);
    ssbo_ = vao_ = prog_ = shadowProg_ = 0;
}
