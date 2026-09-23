#include "sim/agents.hpp"

#include "core/gl_util.hpp"
#include "core/palette.hpp"

#include <algorithm>
#include <chrono>

namespace {

constexpr GLuint kLocalSize     = 256; // must match agents.comp
constexpr GLuint kCullPerThread = 4;   // must match kPerThread in agents.comp
constexpr GLuint kMaxVisible  = 4'000'000;

enum SimLoc : GLint {
    kMode = 0, kCount, kNow, kDayOffsetLoc, kSeed, kCollectStats, kView, kStride, kCapacity, kCounts, kSec,
    kTrainCap = 22
};
enum DrawLoc : GLint { kCenter = 0, kScale, kPointSize, kPpm, kDrawCapacity };
enum TrainLoc : GLint {
    kTCenter = 0, kTScale, kTPpm, kTDayOffset, kTNow, kTTrainCap, kTCounts = 9, kTSec = 10, kTLineColor = 23
};

static_assert(int(kCounts) == int(kTCounts) && int(kSec) == int(kTSec), "shared uniform locations");

constexpr uint32_t kDayMs = 24u * 3600u * 1000u;
constexpr uint32_t kResetAt = 3u * 3600u * 1000u; // 03:00

GLuint makeBuffer(GLsizeiptr bytes, const void* data, GLbitfield flags = 0) {
    GLuint b = 0;
    glCreateBuffers(1, &b);
    glNamedBufferStorage(b, std::max<GLsizeiptr>(bytes, 16), data, flags);
    return b;
}

void deleteBuffer(GLuint& b) {
    if (b) glDeleteBuffers(1, &b);
    b = 0;
}

} // namespace

void Agents::init(const std::string& shaderDir) {
    sim_  = gl::linkProgram({gl::compileShader(GL_COMPUTE_SHADER, shaderDir + "/agents.comp", "#define PASS_SIM\n")});
    cull_ = gl::linkProgram({gl::compileShader(GL_COMPUTE_SHADER, shaderDir + "/agents.comp", "#define PASS_CULL\n")});
    draw_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/agents.vert"),
                             gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/agents.frag")});
    glCreateVertexArrays(1, &vao_); // core profile needs one even with vertex pulling
    stats_ = makeBuffer(sizeof(uint32_t) * 16, nullptr);

    trainProg_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/trains.vert"),
                                  gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/trains.frag")});
    glCreateVertexArrays(1, &trainVao_);
    glVertexArrayAttribIFormat(trainVao_, 0, 2, GL_UNSIGNED_INT, 0);
    glVertexArrayAttribBinding(trainVao_, 0, 0);
    glVertexArrayBindingDivisor(trainVao_, 0, 1);
    glEnableVertexArrayAttrib(trainVao_, 0);

    float colors[27];
    for (int i = 0; i < 9; ++i) {
        const uint32_t c = palette::kRail[i % palette::kRailCount];
        colors[i * 3 + 0] = float((c >> 16) & 0xFFu) / 255.0f;
        colors[i * 3 + 1] = float((c >> 8) & 0xFFu) / 255.0f;
        colors[i * 3 + 2] = float(c & 0xFFu) / 255.0f;
    }
    glProgramUniform3fv(trainProg_, kTLineColor, 9, colors);
    glProgramUniform1ui(trainProg_, kTDayOffset, kDayOffset);
}

void Agents::setWorld(const CityMap& map) {
    world_ = buildSimWorld(map);
    deleteBuffer(worldBuf_);
    worldBuf_ = makeBuffer(GLsizeiptr(world_.data.size() * sizeof(uint32_t)), world_.data.data());

    for (GLuint prog : {sim_, cull_, trainProg_}) {
        glProgramUniform4ui(prog, kCounts, world_.residentialCount, world_.workCount, world_.stationCount,
                            world_.lineCount);
        glProgramUniform1uiv(prog, kSec, kSectionCount, world_.sec.data());
    }
    glProgramUniform3ui(sim_, 28, world_.workByPay[0], world_.workByPay[1], world_.workByPay[2]);

    deleteBuffer(occupancy_);
    occupancy_ = makeBuffer(GLsizeiptr(std::max<uint32_t>(world_.occupancyCount, 1)) * 4, nullptr);
    deleteBuffer(trainSlots_);
    trainSlots_ = makeBuffer(GLsizeiptr(world_.trainSlots.size() * 4), world_.trainSlots.data());
    slotCount_  = GLuint(world_.trainSlots.size() / 2);
    glVertexArrayVertexBuffer(trainVao_, 0, trainSlots_, 0, 8);
}

void Agents::reset(GLuint count, GLuint seed) {
    for (GLuint* b : {&status_, &legs_, &plans_, &cells_, &visible_}) deleteBuffer(*b);
    count_    = std::max<GLuint>(count, 1);
    capacity_ = std::min(count_, kMaxVisible);
    seed_     = seed;
    // Fixed headways, so train capacity scales with the population (one of 8 lines
    // stands for a whole corridor, hence the large numbers).
    trainCap_ = std::max(count_ / 800u, 40u);

    status_  = makeBuffer(GLsizeiptr(count_) * 8, nullptr);
    legs_    = makeBuffer(GLsizeiptr(count_) * 16, nullptr);
    plans_   = makeBuffer(GLsizeiptr(count_) * 8, nullptr);
    cells_   = makeBuffer(GLsizeiptr(count_) * 4, nullptr);
    visible_ = makeBuffer(16 + GLsizeiptr(capacity_) * 16, nullptr);
    if (glGetError() == GL_OUT_OF_MEMORY) throw std::runtime_error("out of GPU memory, try fewer agents");

    for (GLuint prog : {sim_, cull_}) {
        glProgramUniform1ui(prog, kCount, count_);
        glProgramUniform1ui(prog, kSeed, seed_);
        glProgramUniform1ui(prog, kDayOffsetLoc, kDayOffset);
        glProgramUniform1ui(prog, kCapacity, capacity_);
    }
    glProgramUniform1ui(draw_, kDrawCapacity, capacity_);
    glProgramUniform1ui(sim_, kTrainCap, trainCap_);
    glProgramUniform1ui(trainProg_, kTTrainCap, trainCap_);

    const GLuint zero = 0;
    glClearNamedBufferData(occupancy_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glClearNamedBufferData(stats_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glClearNamedBufferData(cells_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    occupancyDay_ = (kDayOffset - kResetAt) / kDayMs;

    const float none[4] = {0, 0, 0, 0};
    run(0, 0, false, none, 1, true, nullptr);
}

void Agents::update(uint32_t nowMs, bool simulate, const Camera& cam, int fbW, int fbH, GLuint stride,
                    bool collectStats, double* timings) {
    glProgramUniform1ui(draw_, 5, nowMs);
    const float margin = 12.0f / cam.ppm + 6.0f;
    const float hw = 0.5f * float(fbW) / cam.ppm + margin, hh = 0.5f * float(fbH) / cam.ppm + margin;
    const float view[4] = {cam.cx - hw, cam.cy - hh, cam.cx + hw, cam.cy + hh};
    // Passenger counters are cleared at 03:00 (no trains run), removing drift from abandoned trips.
    const uint32_t day = (nowMs + kDayOffset - kResetAt) / kDayMs;
    if (day != occupancyDay_) {
        const GLuint zero = 0;
        glClearNamedBufferData(occupancy_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
        occupancyDay_ = day;
    }
    run(1, nowMs, simulate, view, std::max<GLuint>(stride, 1), collectStats, timings);
}

void Agents::run(int mode, uint32_t nowMs, bool simulate, const float view[4], GLuint stride, bool collectStats,
                 double* timings) {
    using clock = std::chrono::steady_clock;
    auto ms     = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    // Reset the indirect command {count 0, instances 1, first 0, base 0} and the counters.
    const GLuint zero = 0, one = 1;
    glClearNamedBufferSubData(visible_, GL_R32UI, 0, 16, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glClearNamedBufferSubData(visible_, GL_R32UI, 4, 4, GL_RED_INTEGER, GL_UNSIGNED_INT, &one);
    // [0,6) and [7]: counted by the cull pass. [6]: cumulative. [8], [9]: cleared by Traffic.
    glClearNamedBufferSubData(stats_, GL_R32UI, 0, 6 * 4, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glClearNamedBufferSubData(stats_, GL_R32UI, 7 * 4, 4, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);

    for (GLuint prog : {sim_, cull_}) {
        glProgramUniform1i(prog, kMode, mode);
        glProgramUniform1ui(prog, kNow, nowMs);
    }
    glProgramUniform4f(cull_, kView, view[0], view[1], view[2], view[3]);
    glProgramUniform1ui(cull_, kCollectStats, collectStats ? 1u : 0u);
    glProgramUniform1ui(cull_, kStride, stride);

    bind();
    if (timings) glFinish();
    auto t0 = clock::now();
    if (mode == 0 || simulate) {
        glUseProgram(sim_);
        dispatch();
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    if (timings) glFinish();
    auto t1 = clock::now();
    glUseProgram(cull_);
    dispatch(kCullPerThread);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
    if (timings) {
        glFinish();
        timings[0] = ms(t0, t1);
        timings[1] = ms(t1, clock::now());
    }
}

void Agents::draw(const Camera& cam, int fbW, int fbH, float pointSize) const {
    glProgramUniform2f(draw_, kCenter, cam.cx, cam.cy);
    glProgramUniform2f(draw_, kScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(draw_, kPointSize, pointSize);
    glProgramUniform1f(draw_, kPpm, cam.ppm);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, visible_);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, visible_);
    glUseProgram(draw_);
    glBindVertexArray(vao_);
    glDrawArraysIndirect(GL_POINTS, nullptr);
}

void Agents::drawTrains(const Camera& cam, int fbW, int fbH, uint32_t nowMs) const {
    if (slotCount_ == 0) return;
    glProgramUniform2f(trainProg_, kTCenter, cam.cx, cam.cy);
    glProgramUniform2f(trainProg_, kTScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(trainProg_, kTPpm, cam.ppm);
    glProgramUniform1ui(trainProg_, kTNow, nowMs);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, worldBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, occupancy_);
    glUseProgram(trainProg_);
    glBindVertexArray(trainVao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(slotCount_));
}

AgentRaw Agents::readAgent(uint32_t id) const {
    AgentRaw a;
    a.id = id;
    uint32_t st[2];
    glGetNamedBufferSubData(status_, GLintptr(id) * 8, 8, st);
    glGetNamedBufferSubData(legs_, GLintptr(id) * 16, 16, a.leg);
    glGetNamedBufferSubData(plans_, GLintptr(id) * 8, 8, a.plan);
    a.ev    = st[0];
    a.state = st[1];
    return a;
}

Agents::Stats Agents::readStats() const {
    uint32_t s[16] = {};
    glGetNamedBufferSubData(stats_, 0, sizeof s, s);
    return {s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9]};
}

void Agents::destroy() {
    for (GLuint* b : {&status_, &legs_, &plans_, &cells_, &worldBuf_, &visible_, &stats_, &occupancy_, &trainSlots_})
        deleteBuffer(*b);
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &trainVao_);
    glDeleteProgram(sim_);
    glDeleteProgram(cull_);
    glDeleteProgram(draw_);
    glDeleteProgram(trainProg_);
}

void Agents::bind() const {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, status_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, legs_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, plans_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, worldBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, visible_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, stats_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, occupancy_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, cells_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, carLane_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, carRoute_);
}

// 2D dispatch: more than 65535 workgroups per dimension is not allowed.
void Agents::dispatch(GLuint perThread) const {
    const GLuint batch  = kLocalSize * perThread;
    const GLuint groups = (count_ + batch - 1) / batch;
    const GLuint gx     = std::min<GLuint>(groups, 65535u);
    const GLuint gy     = (groups + gx - 1) / gx;
    glDispatchCompute(gx, gy, 1);
}
