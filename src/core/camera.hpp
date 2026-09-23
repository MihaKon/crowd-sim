#pragma once
#include <algorithm>

// 2D orthographic camera. World: metres, y up. Screen: pixels, y down.
struct Camera {
    float cx = 0.0f, cy = 0.0f;
    float ppm = 1.0f; // pixels per metre

    static constexpr float kMinPpm = 0.01f;
    static constexpr float kMaxPpm = 200.0f;

    void pan(float dxPx, float dyPx) {
        cx -= dxPx / ppm;
        cy += dyPx / ppm;
    }

    void zoomAt(float factor, float sx, float sy, float vw, float vh) {
        const float ox = sx - vw * 0.5f;
        const float oy = sy - vh * 0.5f;
        const float wx = cx + ox / ppm;
        const float wy = cy - oy / ppm;
        ppm = std::clamp(ppm * factor, kMinPpm, kMaxPpm);
        cx = wx - ox / ppm;
        cy = wy + oy / ppm;
    }
};
