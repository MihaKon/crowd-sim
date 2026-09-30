#include "render/tree_render.hpp"

#include "core/gl_util.hpp"

#include <algorithm>

namespace {

constexpr int   kTiles    = 16;
constexpr float kFadeFrom = 0.45f; // px per metre: crowns fade in between these
constexpr float kFadeTo   = 0.8f;
constexpr float kReach    = 20.0f; // m: crown, lift and shadow around the trunk, for culling

// std430, must match trees.vert
struct Gpu {
    float    x, y, radius;
    uint32_t info; // bits 0-1 kind, bits 2-31 seed
};
static_assert(sizeof(Gpu) == 16);

enum Loc : GLint { kCenter = 0, kScale, kFade, kStrength };

float fadeAt(float ppm) { return std::clamp((ppm - kFadeFrom) / (kFadeTo - kFadeFrom), 0.0f, 1.0f); }

} // namespace

void TreeRenderer::init(const std::string& dir) {
    prog_       = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, dir + "/trees.vert"),
                                   gl::compileShader(GL_FRAGMENT_SHADER, dir + "/trees.frag")});
    shadowProg_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, dir + "/trees.vert", "#define SHADOW\n"),
                                   gl::compileShader(GL_FRAGMENT_SHADER, dir + "/trees.frag", "#define SHADOW\n")});
    glCreateVertexArrays(1, &vao_);
}

void TreeRenderer::upload(const CityMap& map) {
    std::vector<std::vector<Gpu>> bins(size_t(kTiles) * kTiles);
    uint32_t                      seed = 0x2545F491u;
    for (const Tree& t : map.trees) {
        const int tx = std::clamp(int(t.pos.x / map.size * kTiles), 0, kTiles - 1);
        const int ty = std::clamp(int(t.pos.y / map.size * kTiles), 0, kTiles - 1);
        seed         = seed * 1664525u + 1013904223u;
        bins[size_t(ty) * kTiles + size_t(tx)].push_back(
            {t.pos.x, t.pos.y, t.radius, uint32_t(t.kind) | (seed & ~3u)});
    }
    std::vector<Gpu> all;
    ranges_.clear();
    for (int ty = kTiles - 1; ty >= 0; --ty)
        for (int tx = 0; tx < kTiles; ++tx) {
            auto& bin = bins[size_t(ty) * kTiles + size_t(tx)];
            if (bin.empty()) continue;
            std::sort(bin.begin(), bin.end(), [](const Gpu& a, const Gpu& b) { return a.y > b.y; });
            Range r{GLint(all.size()) * 6, GLsizei(bin.size()) * 6, 1e30f, 1e30f, -1e30f, -1e30f};
            for (const Gpu& g : bin) {
                r.minX = std::min(r.minX, g.x - kReach);
                r.minY = std::min(r.minY, g.y - kReach);
                r.maxX = std::max(r.maxX, g.x + kReach);
                r.maxY = std::max(r.maxY, g.y + kReach);
            }
            ranges_.push_back(r);
            all.insert(all.end(), bin.begin(), bin.end());
        }
    if (ssbo_) glDeleteBuffers(1, &ssbo_);
    glCreateBuffers(1, &ssbo_);
    glNamedBufferStorage(ssbo_, GLsizeiptr(std::max<size_t>(1, all.size()) * sizeof(Gpu)), all.data(), 0);
}

bool TreeRenderer::collect(const Camera& cam, int fbW, int fbH) const {
    if (cam.ppm < kFadeFrom || ranges_.empty()) return false;
    const float hw = 0.5f * float(fbW) / cam.ppm, hh = 0.5f * float(fbH) / cam.ppm;
    const float x0 = cam.cx - hw, x1 = cam.cx + hw, y0 = cam.cy - hh, y1 = cam.cy + hh;
    firsts_.clear();
    counts_.clear();
    for (const Range& r : ranges_)
        if (r.maxX >= x0 && r.minX <= x1 && r.maxY >= y0 && r.minY <= y1) {
            firsts_.push_back(r.first);
            counts_.push_back(r.count);
        }
    return !firsts_.empty();
}

void TreeRenderer::drawShadows(const Camera& cam, int fbW, int fbH, float strength) const {
    const float fade = fadeAt(cam.ppm);
    if (strength * fade <= 0.01f || !collect(cam, fbW, fbH)) return;
    glUseProgram(shadowProg_);
    glProgramUniform2f(shadowProg_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(shadowProg_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(shadowProg_, kStrength, strength * fade);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, ssbo_);
    glBindVertexArray(vao_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO); // multiplied onto the ground
    glMultiDrawArrays(GL_TRIANGLES, firsts_.data(), counts_.data(), GLsizei(firsts_.size()));
    glDisable(GL_BLEND);
}

void TreeRenderer::draw(const Camera& cam, int fbW, int fbH) const {
    if (!collect(cam, fbW, fbH)) return;
    glUseProgram(prog_);
    glProgramUniform2f(prog_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(prog_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(prog_, kFade, fadeAt(cam.ppm));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, ssbo_);
    glBindVertexArray(vao_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMultiDrawArrays(GL_TRIANGLES, firsts_.data(), counts_.data(), GLsizei(firsts_.size()));
    glDisable(GL_BLEND);
}

void TreeRenderer::destroy() {
    glDeleteBuffers(1, &ssbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(prog_);
    glDeleteProgram(shadowProg_);
    ssbo_ = vao_ = prog_ = shadowProg_ = 0;
}
