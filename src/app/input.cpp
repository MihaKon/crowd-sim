#include "app/input.hpp"

#include "app/app.hpp"

#include <glad/gl.h>

#include <cmath>

namespace {

constexpr double kClickSlopPx = 4.0;

void onFramebufferSize(GLFWwindow* w, int width, int height) {
    auto& a = app(w);
    a.fbW   = width;
    a.fbH   = height;
    glViewport(0, 0, width, height);
}

void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    auto& a    = app(w);
    a.dragging = (action == GLFW_PRESS);
    glfwGetCursorPos(w, &a.lastX, &a.lastY);
    if (action == GLFW_PRESS) {
        a.pressX = a.lastX;
        a.pressY = a.lastY;
    } else if (std::hypot(a.lastX - a.pressX, a.lastY - a.pressY) < kClickSlopPx) {
        a.clickPending = true;
        a.clickX       = a.lastX;
        a.clickY       = a.lastY;
    }
}

void onCursorPos(GLFWwindow* w, double x, double y) {
    auto& a = app(w);
    if (a.dragging && std::hypot(x - a.pressX, y - a.pressY) >= kClickSlopPx) a.follow = false;
    if (a.dragging) {
        const float r = pixelRatio(w);
        a.cam.pan(float(x - a.lastX) * r, float(y - a.lastY) * r);
    }
    a.lastX = x;
    a.lastY = y;
}

void onScroll(GLFWwindow* w, double, double yoff) {
    auto&  a = app(w);
    double x, y;
    glfwGetCursorPos(w, &x, &y);
    const float r = pixelRatio(w);
    a.zoomTarget  = 0.0f;
    if (a.follow) {
        x = 0.5 * double(a.fbW) / r;
        y = 0.5 * double(a.fbH) / r;
    }
    a.cam.zoomAt(std::pow(1.15f, float(yoff)), float(x) * r, float(y) * r, float(a.fbW), float(a.fbH));
}

uint32_t nextLayer(uint32_t mode) {
    switch (mode) {
    case HeatMap::kOutdoors: return HeatMap::kEveryone;
    case HeatMap::kEveryone: return HeatMap::kTraffic;
    case HeatMap::kTraffic:  return HeatMap::kOff;
    default:                 return HeatMap::kOutdoors;
    }
}

void onKey(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto& a = app(w);
    switch (key) {
    case GLFW_KEY_ESCAPE:
        if (a.inspecting()) {
            a.selAgent = a.selCar = Selection::kNone;
            a.follow              = false;
        } else {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
        break;
    case GLFW_KEY_F:
        if (a.inspecting()) {
            a.follow = !a.follow;
            if (a.follow) a.zoomTarget = std::max(a.cam.ppm, 1.2f);
        }
        break;
    case GLFW_KEY_P:     a.randomPick = true; break;
    case GLFW_KEY_H:     a.showUi = !a.showUi; break;
    case GLFW_KEY_D:     a.heatMode = nextLayer(a.heatMode); break;
    case GLFW_KEY_SPACE: a.paused = !a.paused; break;
    case GLFW_KEY_V:
        a.vsync = !a.vsync;
        glfwSwapInterval(a.vsync ? 1 : 0);
        break;
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:        a.timeScale = std::min(a.timeScale * 2.0f, 3840.0f); break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:   a.timeScale = std::max(a.timeScale * 0.5f, 0.25f); break;
    case GLFW_KEY_RIGHT_BRACKET: a.pointSize = std::min(a.pointSize + 1.0f, 16.0f); break;
    case GLFW_KEY_LEFT_BRACKET:  a.pointSize = std::max(a.pointSize - 1.0f, 1.0f); break;
    case GLFW_KEY_R:             resetCamera(a); break;
    case GLFW_KEY_L:             a.lod = !a.lod; break;
    case GLFW_KEY_M:             a.showMap = !a.showMap; break;
    case GLFW_KEY_A:             a.showAgents = !a.showAgents; break;
    case GLFW_KEY_N:
        a.seed       = a.seed * 1664525u + 1013904223u;
        a.regenerate = true;
        break;
    default: break;
    }
}

} // namespace

void installInput(GLFWwindow* win) {
    glfwSetFramebufferSizeCallback(win, onFramebufferSize);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetCursorPosCallback(win, onCursorPos);
    glfwSetScrollCallback(win, onScroll);
    glfwSetKeyCallback(win, onKey);
}
