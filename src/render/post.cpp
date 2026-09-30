#include "render/post.hpp"

#include "core/gl_util.hpp"

#include <algorithm>
#include <cmath>

namespace {

GLuint texture2d(GLenum format, int w, int h) {
    GLuint t = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &t);
    glTextureStorage2D(t, 1, format, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint framebuffer(GLuint colour) {
    GLuint f = 0;
    glCreateFramebuffers(1, &f);
    glNamedFramebufferTexture(f, GL_COLOR_ATTACHMENT0, colour, 0);
    return f;
}

enum Loc : GLint { kTexel = 0, kPrefilter, kExposure, kBloom, kVignette, kWarmth, kNight };

} // namespace

void PostFx::init(const std::string& dir) {
    auto program = [&](const char* frag) {
        return gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, dir + "/fullscreen.vert"),
                                gl::compileShader(GL_FRAGMENT_SHADER, dir + "/" + frag)});
    };
    downProg_      = program("bloom_down.frag");
    upProg_        = program("bloom_up.frag");
    compositeProg_ = program("composite.frag");
    glCreateVertexArrays(1, &vao_);
}

void PostFx::release() {
    glDeleteFramebuffers(1, &sceneFbo_);
    glDeleteFramebuffers(1, &ldrFbo_);
    glDeleteTextures(1, &ldrTex_);
    ldrFbo_ = ldrTex_ = 0;
    glDeleteFramebuffers(kLevels, bloomFbo_);
    glDeleteTextures(1, &sceneTex_);
    glDeleteTextures(kLevels, bloomTex_);
    glDeleteRenderbuffers(1, &depthRb_);
    sceneFbo_ = sceneTex_ = depthRb_ = 0;
    std::fill(std::begin(bloomFbo_), std::end(bloomFbo_), 0u);
    std::fill(std::begin(bloomTex_), std::end(bloomTex_), 0u);
}

void PostFx::resize(int w, int h) {
    release();
    w_ = w;
    h_ = h;
    sceneTex_ = texture2d(GL_R11F_G11F_B10F, w, h); // half the bandwidth of RGBA16F; no alpha needed
    sceneFbo_ = framebuffer(sceneTex_);
    glCreateRenderbuffers(1, &depthRb_);
    glNamedRenderbufferStorage(depthRb_, GL_DEPTH24_STENCIL8, w, h);
    glNamedFramebufferRenderbuffer(sceneFbo_, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRb_);
    if (scale_ < 1.0f) {
        ldrTex_ = texture2d(GL_RGBA8, w, h);
        ldrFbo_ = framebuffer(ldrTex_);
    }
    for (int i = 0; i < kLevels; ++i) {
        bw_[i]        = std::max(1, w >> (i + 2)); // from quarter resolution down
        bh_[i]        = std::max(1, h >> (i + 2));
        bloomTex_[i]  = texture2d(GL_R11F_G11F_B10F, bw_[i], bh_[i]);
        bloomFbo_[i]  = framebuffer(bloomTex_[i]);
    }
}

void PostFx::beginScene(int w, int h, bool fullRes, const float clearRgb[3]) {
    constexpr double kBudget = 2.2e6; // scene pixels: about 1080p
    const float scale = fullRes ? 1.0f : float(std::min(1.0, std::sqrt(kBudget / (double(w) * double(h)))));
    const int   sw = std::max(1, int(float(w) * scale)), sh = std::max(1, int(float(h) * scale));
    if (w != winW_ || h != winH_ || sw != w_ || sh != h_) {
        winW_  = w;
        winH_  = h;
        scale_ = scale;
        resize(sw, sh);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, w_, h_);
    glClearColor(clearRgb[0], clearRgb[1], clearRgb[2], 1.0f);
    glClearStencil(0);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void PostFx::finish(const Params& p) {
    glBindVertexArray(vao_);
    glDisable(GL_BLEND);

    const bool bloom = p.bloom > 0.0f;
    if (bloom) {
        // Down: a soft threshold straight into quarter resolution, then 13-tap steps...
        glUseProgram(downProg_);
        for (int i = 0; i < kLevels; ++i) {
            const GLuint src = i == 0 ? sceneTex_ : bloomTex_[i - 1];
            const int    sw = i == 0 ? w_ : bw_[i - 1], sh = i == 0 ? h_ : bh_[i - 1];
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
            glViewport(0, 0, bw_[i], bh_[i]);
            glBindTextureUnit(0, src);
            glProgramUniform2f(downProg_, kTexel, 1.0f / float(sw), 1.0f / float(sh));
            glProgramUniform1i(downProg_, kPrefilter, i == 0 ? 1 : 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        // ...and back up, each level added onto the next larger one.
        glUseProgram(upProg_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = kLevels - 1; i > 0; --i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i - 1]);
            glViewport(0, 0, bw_[i - 1], bh_[i - 1]);
            glBindTextureUnit(0, bloomTex_[i]);
            glProgramUniform2f(upProg_, kTexel, 1.0f / float(bw_[i]), 1.0f / float(bh_[i]));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glDisable(GL_BLEND);
    }

    // Composite at scene resolution; straight into the window, or into a buffer that
    // is then upscaled with a linear blit.
    glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo_);
    glViewport(0, 0, w_, h_);
    glUseProgram(compositeProg_);
    glBindTextureUnit(0, sceneTex_);
    glBindTextureUnit(1, bloomTex_[0]);
    glProgramUniform2f(compositeProg_, kTexel, 1.0f / float(w_), 1.0f / float(h_));
    glProgramUniform1f(compositeProg_, kExposure, p.exposure);
    glProgramUniform1f(compositeProg_, kBloom, bloom ? p.bloom : 0.0f);
    glProgramUniform1f(compositeProg_, kVignette, p.vignette);
    glProgramUniform1f(compositeProg_, kWarmth, p.warmth);
    glProgramUniform1f(compositeProg_, kNight, p.night);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (ldrFbo_) glBlitNamedFramebuffer(ldrFbo_, 0, 0, 0, w_, h_, 0, 0, winW_, winH_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, winW_, winH_);
}

void PostFx::destroy() {
    release();
    glDeleteVertexArrays(1, &vao_);
    for (GLuint* p : {&downProg_, &upProg_, &compositeProg_}) {
        glDeleteProgram(*p);
        *p = 0;
    }
    vao_ = 0;
    w_ = h_ = winW_ = winH_ = 0;
}
