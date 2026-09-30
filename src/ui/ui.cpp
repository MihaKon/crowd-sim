#include "ui/ui.hpp"

#include "core/gl_util.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {

constexpr int kFirstChar = 32, kCharCount = 95;

uint32_t rgba(uint32_t rgb, float alpha) {
    const uint32_t a = uint32_t(std::lround(std::fmin(std::fmax(alpha, 0.0f), 1.0f) * 255.0f));
    return ((rgb >> 16) & 0xFFu) | (rgb & 0xFF00u) | ((rgb & 0xFFu) << 16) | (a << 24);
}

} // namespace

void Ui::init(const std::string& shaderDir, const std::string& fontPath, float pixelHeight) {
    std::ifstream f(fontPath, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open font " + fontPath);
    const std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), 0)) throw std::runtime_error("bad font " + fontPath);
    const float heights[2] = {pixelHeight, std::round(pixelHeight * 2.0f)};
    for (int i = 0; i < 2; ++i) {
        const float scale = stbtt_ScaleForPixelHeight(&info, heights[i]);
        int ascent, descent, gap, adv, lsb;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        stbtt_GetCodepointHMetrics(&info, 'M', &adv, &lsb);
        font_[i].ascent     = std::round(float(ascent) * scale);
        font_[i].lineHeight = std::round(float(ascent - descent + gap) * scale);
        font_[i].advance    = float(adv) * scale;
        font_[i].packed.resize(sizeof(stbtt_packedchar) * kCharCount);
    }

    std::vector<unsigned char> bitmap;
    for (;;) {
        bitmap.assign(size_t(atlasW_) * atlasH_, 0);
        stbtt_pack_context pc;
        stbtt_PackBegin(&pc, bitmap.data(), atlasW_, atlasH_, 0, 1, nullptr);
        stbtt_PackSetOversampling(&pc, 2, 1);
        int ok = 1;
        for (int i = 0; i < 2 && ok; ++i)
            ok = stbtt_PackFontRange(&pc, ttf.data(), 0, heights[i], kFirstChar, kCharCount,
                                     reinterpret_cast<stbtt_packedchar*>(font_[i].packed.data()));
        stbtt_PackEnd(&pc);
        if (ok) break;
        atlasW_ *= 2;
        atlasH_ *= 2;
        if (atlasW_ > 4096) throw std::runtime_error("font atlas too large");
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &tex_);
    glTextureStorage2D(tex_, 1, GL_R8, atlasW_, atlasH_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(tex_, 0, 0, 0, atlasW_, atlasH_, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTextureParameteri(tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    prog_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/ui.vert"),
                             gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/ui.frag")});
    glCreateVertexArrays(1, &vao_);
    glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, x));
    glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, u));
    glVertexArrayAttribFormat(vao_, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
    glVertexArrayAttribFormat(vao_, 3, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, shape));
    for (GLuint i = 0; i < 4; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
    glCreateBuffers(1, &vbo_);
}

void Ui::begin(int fbW, int fbH) {
    fbW_ = fbW;
    fbH_ = fbH;
    verts_.clear();
}

void Ui::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c) {
    verts_.push_back({x0, y0, u0, v0, c, {0, 0, 0, 0}});
    verts_.push_back({x1, y0, u1, v0, c, {0, 0, 0, 0}});
    verts_.push_back({x1, y1, u1, v1, c, {0, 0, 0, 0}});
    verts_.push_back({x0, y0, u0, v0, c, {0, 0, 0, 0}});
    verts_.push_back({x1, y1, u1, v1, c, {0, 0, 0, 0}});
    verts_.push_back({x0, y1, u0, v1, c, {0, 0, 0, 0}});
}

// A quad around a rounded rectangle, grown by the feather; uv is the offset from its centre.
void Ui::shapeQuad(float cx, float cy, float hw, float hh, float radius, float feather, uint32_t c) {
    const float gx = hw + feather + 1.0f, gy = hh + feather + 1.0f;
    const float s[4] = {hw, hh, std::min(radius, std::min(hw, hh)), feather};
    auto v = [&](float ox, float oy) { verts_.push_back({cx + ox, cy + oy, ox, oy, c, {s[0], s[1], s[2], s[3]}}); };
    v(-gx, -gy);
    v(gx, -gy);
    v(gx, gy);
    v(-gx, -gy);
    v(gx, gy);
    v(-gx, gy);
}

void Ui::rect(float x, float y, float w, float h, uint32_t rgb, float alpha) {
    quad(x, y, x + w, y + h, -1.0f, 0.0f, -1.0f, 0.0f, rgba(rgb, alpha));
}

void Ui::roundRect(float x, float y, float w, float h, float radius, uint32_t rgb, float alpha) {
    shapeQuad(x + 0.5f * w, y + 0.5f * h, 0.5f * w, 0.5f * h, radius, 0.0f, rgba(rgb, alpha));
}

void Ui::shadow(float x, float y, float w, float h, float radius, float blur, float alpha) {
    shapeQuad(x + 0.5f * w, y + 0.5f * h + 0.35f * blur, 0.5f * w, 0.5f * h, radius, blur, rgba(0x000000, alpha));
}

void Ui::circle(float cx, float cy, float r, uint32_t rgb, float alpha) {
    shapeQuad(cx, cy, r, r, r, 0.0f, rgba(rgb, alpha));
}

float Ui::text(float x, float y, const std::string& s, uint32_t rgb, float alpha, Size size) {
    const Font&    f = font_[size];
    const uint32_t c = rgba(rgb, alpha);
    float          px = std::round(x), py = std::round(y + f.ascent);
    for (char ch : s) {
        int idx = int((unsigned char)ch) - kFirstChar;
        if (idx < 0 || idx >= kCharCount) idx = '?' - kFirstChar;
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(reinterpret_cast<const stbtt_packedchar*>(f.packed.data()), atlasW_, atlasH_, idx, &px,
                            &py, &q, 0);
        if (ch != ' ') quad(q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1, c);
    }
    return px;
}

void Ui::end() {
    if (verts_.empty()) return;
    glNamedBufferData(vbo_, GLsizeiptr(verts_.size() * sizeof(Vertex)), verts_.data(), GL_STREAM_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glProgramUniform2f(prog_, 0, float(fbW_), float(fbH_));
    glBindTextureUnit(0, tex_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
    glDisable(GL_BLEND);
}

void Ui::destroy() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteTextures(1, &tex_);
    glDeleteProgram(prog_);
}
