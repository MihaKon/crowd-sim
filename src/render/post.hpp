#pragma once
#include <glad/gl.h>

#include <string>

// The world is drawn into a linear HDR buffer (with depth-stencil for the building
// shadows); finish() adds bloom, tone maps, grades and anti-aliases it in one pass.
// On large windows the scene is rendered at a lower resolution (about 1080p worth
// of pixels) and upscaled into the window at the end; the UI is drawn afterwards,
// at full resolution.
class PostFx {
public:
    struct Params {
        float exposure = 1.0f;
        float bloom    = 0.0f;  // bloom strength; 0 skips the bloom passes
        float vignette = 0.25f;
        float warmth   = 0.0f;  // golden hour grading, 0..1
        float night    = 0.0f;  // night grading, 0..1
    };

    void  init(const std::string& shaderDir);
    // fullRes: render the scene at the window's resolution even when it is large.
    void  beginScene(int w, int h, bool fullRes, const float clearRgb[3]);
    void  finish(const Params& p);
    float renderScale() const { return scale_; } // scene pixels per window pixel
    void destroy();

private:
    static constexpr int kLevels = 6;

    void resize(int w, int h);
    void release();

    int    winW_ = 0, winH_ = 0; // window
    int    w_ = 0, h_ = 0;       // scene
    float  scale_ = 1.0f;
    GLuint ldrFbo_ = 0, ldrTex_ = 0; // tone-mapped scene, when it is upscaled
    GLuint sceneFbo_ = 0, sceneTex_ = 0, depthRb_ = 0;
    GLuint bloomFbo_[kLevels] = {}, bloomTex_[kLevels] = {};
    int    bw_[kLevels] = {}, bh_[kLevels] = {};
    GLuint vao_ = 0, downProg_ = 0, upProg_ = 0, compositeProg_ = 0;
};
