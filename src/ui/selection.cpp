#include "ui/selection.hpp"

#include "core/gl_util.hpp"

#include <algorithm>
#include <cstring>

void Selection::init(const std::string& shaderDir) {
    pickProg_   = gl::linkProgram({gl::compileShader(GL_COMPUTE_SHADER, shaderDir + "/pick.comp")});
    markerProg_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/marker.vert"),
                                   gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/marker.frag")});
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &pickBuf_);
    glNamedBufferStorage(pickBuf_, 16, nullptr, 0);
    glCreateBuffers(1, &selectBuf_);
    glNamedBufferStorage(selectBuf_, 32, nullptr, 0);

    const GLbitfield flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glCreateBuffers(kRing, ring_);
    for (int i = 0; i < kRing; ++i) {
        glNamedBufferStorage(ring_[i], 32, nullptr, flags);
        mapped_[i] = static_cast<uint32_t*>(glMapNamedBufferRange(ring_[i], 0, 32, flags));
    }
}

void Selection::endFrame() {
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    glCopyNamedBufferSubData(selectBuf_, ring_[head_], 0, 0, 32);
    if (fence_[head_]) glDeleteSync(fence_[head_]);
    fence_[head_] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    head_         = (head_ + 1) % kRing;
}

Selection::Tracked Selection::latest() {
    // Newest finished copy first; never wait.
    for (int k = 1; k <= kRing; ++k) {
        const int i = (head_ - k + kRing) % kRing;
        if (!fence_[i]) continue;
        const GLenum r = glClientWaitSync(fence_[i], 0, 0);
        if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED) continue;
        const uint32_t* s = mapped_[i];
        Tracked         t;
        auto            f = [](uint32_t u) {
            float v;
            std::memcpy(&v, &u, 4);
            return v;
        };
        if (s[6] != 0) {
            t = {true, {f(s[4]), f(s[5])}, 4};
        } else if (s[2] != 0) {
            t = {true, {f(s[0]), f(s[1])}, s[3]};
        }
        if (t.valid) last_ = t;
        return t;
    }
    return {};
}

uint32_t Selection::randomVisible(GLuint agentVisible, uint32_t agentCap, uint32_t seed) const {
    uint32_t count = 0;
    glGetNamedBufferSubData(agentVisible, 0, 4, &count);
    count = std::min(count, agentCap);
    if (count == 0) return kNone;
    uint32_t entry[4];
    glGetNamedBufferSubData(agentVisible, 16 + GLintptr((seed * 2654435761u) % count) * 16, 16, entry);
    return entry[2];
}

void Selection::beginFrame() {
    const GLuint zero = 0;
    glClearNamedBufferData(selectBuf_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 16, selectBuf_);
}

Selection::Hit Selection::pick(GLuint agentVisible, uint32_t agentCap, GLuint carVisible, uint32_t carCap, Vec2 point,
                               float radius) {
    const GLuint none = kNone;
    glClearNamedBufferData(pickBuf_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &none);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, agentVisible);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, carVisible);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 17, pickBuf_);
    glUseProgram(pickProg_);
    glProgramUniform2f(pickProg_, 0, point.x, point.y);
    glProgramUniform1f(pickProg_, 1, radius);
    glProgramUniform1ui(pickProg_, 2, agentCap);
    glProgramUniform1ui(pickProg_, 3, carCap);
    const GLuint groups = (std::max(agentCap, carCap) + 255) / 256;
    const GLuint gx     = std::min<GLuint>(std::max<GLuint>(groups, 1), 65535u);
    glDispatchCompute(gx, (groups + gx - 1) / gx, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    uint32_t best[2];
    glGetNamedBufferSubData(pickBuf_, 0, sizeof best, best);
    Hit hit;
    uint32_t entry[4];
    if (best[0] != kNone) {
        glGetNamedBufferSubData(agentVisible, 16 + GLintptr(best[0] & 0x3FFFFFu) * 16, sizeof entry, entry);
        hit.agent     = entry[2];
        hit.agentDist = float(best[0] >> 22) / 1023.0f * radius;
    }
    if (best[1] != kNone) {
        glGetNamedBufferSubData(carVisible, 16 + GLintptr(best[1] & 0x3FFFFFu) * 16, sizeof entry, entry);
        hit.car     = entry[2];
        hit.carDist = float(best[1] >> 22) / 1023.0f * radius;
    }
    return hit;
}

void Selection::drawMarker(const Camera& cam, int fbW, int fbH, float sizePx) const {
    glUseProgram(markerProg_);
    glProgramUniform2f(markerProg_, 0, cam.cx, cam.cy);
    glProgramUniform2f(markerProg_, 1, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(markerProg_, 2, cam.ppm);
    glProgramUniform1f(markerProg_, 3, sizePx);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 16, selectBuf_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Selection::destroy() {
    for (int i = 0; i < kRing; ++i) {
        if (fence_[i]) glDeleteSync(fence_[i]);
        glUnmapNamedBuffer(ring_[i]);
    }
    glDeleteBuffers(kRing, ring_);
    glDeleteBuffers(1, &pickBuf_);
    glDeleteBuffers(1, &selectBuf_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(pickProg_);
    glDeleteProgram(markerProg_);
}
