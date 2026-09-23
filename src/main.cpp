#include <glad/gl.h>

#include "app/app.hpp"
#include "app/input.hpp"
#include "app/runner.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

namespace {

double parseNumber(int argc, char** argv, int i, double fallback) {
    if (argc <= i) return fallback;
    const double v = std::strtod(argv[i], nullptr); // accepts "5e6"
    return v >= 0.0 ? v : fallback;
}

#ifndef NDEBUG
void GLAD_API_PTR onGlDebug(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* msg, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::fprintf(stderr, "[GL%s] %s\n", type == GL_DEBUG_TYPE_ERROR ? " ERROR" : "", msg);
}
#endif

GLFWwindow* createWindow() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(1280, 720, "crowd-sim", nullptr, nullptr);
    if (!win) throw std::runtime_error("cannot create GL 4.6 core window");
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress)) throw std::runtime_error("gladLoadGL failed");
    glfwSwapInterval(1);
#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(onGlDebug, nullptr);
#endif
    return win;
}

// The largest per-agent buffer (legs, 16 B each) must fit in one SSBO.
GLuint agentCount(double requested) {
    GLint64 maxSsbo = 0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxSsbo);
    const uint64_t maxAgents = std::min<uint64_t>(uint64_t(maxSsbo) / 16, INT32_MAX);
    uint64_t       n         = uint64_t(requested);
    if (n > maxAgents) {
        std::printf("clamping %llu -> %llu agents (SSBO limit)\n", static_cast<unsigned long long>(n),
                    static_cast<unsigned long long>(maxAgents));
        n = maxAgents;
    }
    return GLuint(std::max<uint64_t>(n, 1));
}

} // namespace

int main(int argc, char** argv) try {
    if (!glfwInit()) {
        std::fputs("glfwInit failed\n", stderr);
        return 1;
    }
    GLFWwindow* win = createWindow();
    std::printf("crowd-sim\nGL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    App a;
    a.seed = uint32_t(parseNumber(argc, argv, 2, 1.0));
    glfwSetWindowUserPointer(win, &a);
    glfwGetFramebufferSize(win, &a.fbW, &a.fbH);
    glViewport(0, 0, a.fbW, a.fbH);
    resetCamera(a);
    installInput(win);

    const GLuint count = agentCount(parseNumber(argc, argv, 1, 1e6));
    std::printf("agents: %u (%.0f MB agent state)\n", count, double(count) * 36.0 / (1 << 20));
    {
        Runner runner(win, a, count);
        runner.run();
    }
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    glfwTerminate();
    return 1;
}
