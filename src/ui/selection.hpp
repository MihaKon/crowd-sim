#pragma once
#include "core/camera.hpp"
#include "core/math.hpp"

#include <glad/gl.h>
#include <cstdint>
#include <string>

// Click picking and the marker that follows the inspected person / car. The simulation
// kernels write its position on the GPU, so drawing the marker never waits for a readback.
class Selection {
public:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    struct Hit {
        uint32_t agent = kNone, car = kNone;
        float    agentDist = 1e30f, carDist = 1e30f;
    };

    struct Tracked {
        bool     valid = false;
        Vec2     pos;
        uint32_t kind = 0; // agent leg kind (0 indoors, 1 walking, 2 platform, 3 train), or 4 for a car
    };

    void init(const std::string& shaderDir);
    void beginFrame();
    void endFrame();
    Tracked latest();
    uint32_t randomVisible(GLuint agentVisible, uint32_t agentCap, uint32_t seed) const;
    // Stalls the GPU: call on clicks only.
    Hit  pick(GLuint agentVisible, uint32_t agentCap, GLuint carVisible, uint32_t carCap, Vec2 point, float radius);
    void drawMarker(const Camera& cam, int fbW, int fbH, float sizePx) const;
    void destroy();

private:
    static constexpr int kRing = 3;
    GLuint   pickProg_ = 0, markerProg_ = 0, vao_ = 0, pickBuf_ = 0, selectBuf_ = 0;
    GLuint   ring_[kRing]{};
    GLsync   fence_[kRing]{};
    uint32_t* mapped_[kRing]{};
    int      head_ = 0;
    Tracked  last_;
};
