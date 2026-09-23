#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "core/camera.hpp"
#include "render/heatmap.hpp"
#include "ui/selection.hpp"

#include <algorithm>
#include <cstdint>

constexpr float kWorldSize = 20'000.0f;

// State shared by the input callbacks, the HUD and the frame loop.
struct App {
    Camera   cam;
    int      fbW = 1280, fbH = 720;
    bool     dragging = false;
    double   lastX = 0.0, lastY = 0.0;
    bool     paused = false;
    bool     vsync = true;
    float    timeScale = 60.0f;
    float    pointSize = 2.0f;
    bool     lod = true;
    bool     showMap = true;
    bool     showAgents = true;
    uint32_t seed = 1;
    bool     regenerate = false;

    uint32_t heatMode = HeatMap::kOutdoors;
    bool     showUi = true;
    bool     clickPending = false;
    double   pressX = 0.0, pressY = 0.0, clickX = 0.0, clickY = 0.0;
    uint32_t selAgent = Selection::kNone, selCar = Selection::kNone;
    bool     follow = false;
    float    zoomTarget = 0.0f;
    bool     randomPick = false;
    float    panel[4] = {0, 0, 0, 0};

    bool inspecting() const { return selAgent != Selection::kNone || selCar != Selection::kNone; }
};

inline App& app(GLFWwindow* w) { return *static_cast<App*>(glfwGetWindowUserPointer(w)); }

// Window coordinates to framebuffer pixels (HiDPI, fractional scaling).
inline float pixelRatio(GLFWwindow* w) {
    int ww, wh, fw, fh;
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetFramebufferSize(w, &fw, &fh);
    return ww > 0 ? float(fw) / float(ww) : 1.0f;
}

inline void resetCamera(App& a) {
    a.cam.cx = a.cam.cy = kWorldSize * 0.5f;
    a.cam.ppm = float(std::max(1, std::min(a.fbW, a.fbH))) / kWorldSize;
}
