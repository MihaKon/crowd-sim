#include "sim/traffic.hpp"

#include "core/gl_util.hpp"

#include <algorithm>
#include <random>
#include <vector>

namespace {

constexpr GLuint   kLocalSize  = LOCAL_SIZE;
constexpr uint32_t kMaxVisible = 1'000'000;
constexpr uint32_t kNone       = 0xFFFFFFFFu;

enum Loc : GLint {
    kNow = TRAFFIC_LOC_NOW, kDt = TRAFFIC_LOC_DT, kLaneCount = TRAFFIC_LOC_LANE_COUNT,
    kDayOffset = TRAFFIC_LOC_DAY_OFFSET, kCarCount = TRAFFIC_LOC_CAR_COUNT, kOwnerCount = TRAFFIC_LOC_OWNER_COUNT,
    kView = TRAFFIC_LOC_VIEW, kStride = TRAFFIC_LOC_STRIDE, kCapacity = TRAFFIC_LOC_CAPACITY,
    kCounts = LOC_COUNTS, kSec = LOC_SEC,
    kAdvance = TRAFFIC_LOC_ADVANCE, kEmit = TRAFFIC_LOC_EMIT, kCollect = TRAFFIC_LOC_COLLECT, kTSec = TRAFFIC_LOC_TSEC,
    kSeed = TRAFFIC_LOC_SEED, kNodeCount = TRAFFIC_LOC_NODE_COUNT
};
enum DrawLoc : GLint { kDCenter = 0, kDScale, kDPpm, kDCapacity, kDOwnerCount };

GLuint makeBuffer(GLsizeiptr bytes, const void* data) {
    GLuint b = 0;
    glCreateBuffers(1, &b);
    glNamedBufferStorage(b, std::max<GLsizeiptr>(bytes, 16), data, 0);
    return b;
}

void deleteBuffer(GLuint& b) {
    if (b) glDeleteBuffers(1, &b);
    b = 0;
}

void fill(GLuint buf, GLuint value) {
    glClearNamedBufferData(buf, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &value);
}

GLuint compute(const std::string& dir, const char* pass) {
    return gl::linkProgram({gl::compileShader(GL_COMPUTE_SHADER, dir + "/traffic.comp",
                                              std::string("#define ") + pass + "\n")});
}

} // namespace

void Traffic::init(const std::string& shaderDir) {
    spawn_  = compute(shaderDir, "PASS_SPAWN");
    move_   = compute(shaderDir, "PASS_MOVE");
    commit_ = compute(shaderDir, "PASS_COMMIT");
    draw_   = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/cars.vert"),
                               gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/cars.frag")});
    congestion_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/congestion.vert"),
                                   gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/congestion.frag")});
    glCreateVertexArrays(1, &vao_);
}

void Traffic::setWorld(const CityMap& map, const SimWorld& sim, GLuint worldBuf) {
    world_     = buildTrafficWorld(map);
    worldBuf_  = worldBuf;
    nodeCount_ = uint32_t(map.nodes.size());

    for (GLuint* b : {&trafficBuf_, &laneState_, &laneQueue_, &laneReq_, &laneStat_}) deleteBuffer(*b);
    nodeSec_ = sim.sec[kSecNodePos];
    trafficBuf_ = makeBuffer(GLsizeiptr(world_.data.size() * 4), world_.data.data());
    laneState_  = makeBuffer(GLsizeiptr(world_.laneCount) * 8, nullptr);
    laneQueue_  = makeBuffer(GLsizeiptr(world_.queueSize) * 4, nullptr);
    laneReq_    = makeBuffer(GLsizeiptr(world_.laneCount) * 8, nullptr);
    laneStat_   = makeBuffer(GLsizeiptr(world_.laneCount) * 4, nullptr);
    fill(laneStat_, 0);

    for (GLuint prog : {spawn_, move_, commit_}) {
        glProgramUniform1ui(prog, kLaneCount, world_.laneCount);
        glProgramUniform1ui(prog, kDayOffset, DAY_START_MS);
        glProgramUniform4ui(prog, kCounts, sim.residentialCount, sim.workCount, sim.stationCount, sim.lineCount);
        glProgramUniform1uiv(prog, kSec, kSectionCount, sim.sec.data());
        glProgramUniform1uiv(prog, kTSec, kTSectionCount, world_.sec.data());
        glProgramUniform1ui(prog, kNodeCount, nodeCount_);
    }
}

void Traffic::reset(uint32_t agentCount, uint32_t seed, GLuint statusBuf, GLuint statsBuf) {
    statusBuf_  = statusBuf;
    statsBuf_   = statsBuf;
    const uint32_t maxOwners = std::max(1u, world_.queueSize / kSlotsPerOwner);
    ownerEvery_ = std::max(kOwnerEvery, (agentCount + maxOwners - 1) / maxOwners);
    ownerCount_ = (agentCount + ownerEvery_ - 1) / ownerEvery_;
    carCount_   = ownerCount_ + std::min(agentCount / kFleetEvery, world_.queueSize / kSlotsPerFleet);
    capacity_   = std::max(1u, std::min(carCount_, kMaxVisible));

    for (GLuint* b : {&carLane_, &carKin_, &carRoute_, &visible_}) deleteBuffer(*b);

    std::vector<uint32_t> route(size_t(carCount_) * 4, 0);
    std::mt19937          rng(seed ^ 0x5eedca75u);
    for (uint32_t c = 0; c < carCount_; ++c) {
        uint32_t* r = &route[size_t(c) * 4];
        r[2]        = kNone;
        if (c >= ownerCount_) {
            r[1] = rng() % std::max(1u, nodeCount_);
            r[3] = rng() % (3600u * 1000u);
        }
    }
    carLane_  = makeBuffer(GLsizeiptr(carCount_) * 4, nullptr);
    carKin_   = makeBuffer(GLsizeiptr(carCount_) * 16, nullptr);
    carRoute_ = makeBuffer(GLsizeiptr(route.size() * 4), route.data());
    visible_  = makeBuffer(16 + GLsizeiptr(capacity_) * 16, nullptr);
    fill(carLane_, kNone);
    fill(carKin_, 0);
    fill(laneState_, 0);
    fill(laneReq_, kNone);

    for (GLuint prog : {spawn_, move_, commit_}) {
        glProgramUniform1ui(prog, kCarCount, carCount_);
        glProgramUniform1ui(prog, kOwnerCount, ownerCount_);
        glProgramUniform1ui(prog, kCapacity, capacity_);
        glProgramUniform1ui(prog, kSeed, seed);
    }
    glProgramUniform1ui(draw_, kDCapacity, capacity_);
    glProgramUniform1ui(draw_, kDOwnerCount, ownerCount_);
}

void Traffic::step(uint32_t nowMs, float dtS, bool advance, bool emit, bool collect, const Camera& cam, int fbW,
                   int fbH, GLuint stride) {
    const GLuint zero = 0, one = 1;
    if (emit) {
        glClearNamedBufferSubData(visible_, GL_R32UI, 0, 16, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
        glClearNamedBufferSubData(visible_, GL_R32UI, 4, 4, GL_RED_INTEGER, GL_UNSIGNED_INT, &one);
    }
    if (collect) glClearNamedBufferSubData(statsBuf_, GL_R32UI, 8 * 4, 8, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);

    const float margin = 6.0f / cam.ppm + 12.0f;
    const float hw = 0.5f * float(fbW) / cam.ppm + margin, hh = 0.5f * float(fbH) / cam.ppm + margin;
    for (GLuint prog : {spawn_, move_, commit_}) {
        glProgramUniform1ui(prog, kNow, nowMs);
        glProgramUniform1f(prog, kDt, dtS);
    }
    glProgramUniform1ui(move_, kAdvance, advance ? 1u : 0u);
    glProgramUniform1ui(move_, kEmit, emit ? 1u : 0u);
    glProgramUniform1ui(move_, kCollect, collect ? 1u : 0u);
    glProgramUniform4f(move_, kView, cam.cx - hw, cam.cy - hh, cam.cx + hw, cam.cy + hh);
    glProgramUniform1ui(move_, kStride, std::max<GLuint>(stride, 1));

    bind();
    if (advance) {
        dispatch(spawn_, carCount_);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    dispatch(move_, world_.laneCount);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    if (advance) {
        dispatch(commit_, world_.laneCount);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    if (emit) glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
}

void Traffic::drawCongestion(const Camera& cam, int fbW, int fbH, float opacity) const {
    if (opacity <= 0.0f || world_.laneCount == 0) return;
    glUseProgram(congestion_);
    glProgramUniform2f(congestion_, 0, cam.cx, cam.cy);
    glProgramUniform2f(congestion_, 1, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(congestion_, 2, cam.ppm);
    glProgramUniform1ui(congestion_, 3, nodeSec_);
    glProgramUniform1ui(congestion_, 4, world_.sec[kTSecLanes]);
    glProgramUniform1f(congestion_, 5, opacity);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, worldBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, trafficBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, laneStat_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(world_.laneCount * 6));
    glDisable(GL_BLEND);
}

CarRaw Traffic::readCar(uint32_t car) const {
    CarRaw r;
    r.car = car;
    glGetNamedBufferSubData(carLane_, GLintptr(car) * 4, 4, &r.lane);
    glGetNamedBufferSubData(carKin_, GLintptr(car) * 16, 16, r.kin);
    glGetNamedBufferSubData(carRoute_, GLintptr(car) * 16, 16, r.route);
    return r;
}

void Traffic::draw(const Camera& cam, int fbW, int fbH) const {
    glProgramUniform2f(draw_, kDCenter, cam.cx, cam.cy);
    glProgramUniform2f(draw_, kDScale, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(draw_, kDPpm, cam.ppm);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, visible_);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, visible_);
    glUseProgram(draw_);
    glBindVertexArray(vao_);
    glDrawArraysIndirect(GL_TRIANGLES, nullptr);
}

void Traffic::destroy() {
    for (GLuint* b : {&trafficBuf_, &laneState_, &laneQueue_, &laneReq_, &carLane_, &carKin_, &carRoute_, &visible_,
                      &laneStat_})
        deleteBuffer(*b);
    glDeleteProgram(congestion_);
    glDeleteVertexArrays(1, &vao_);
    for (GLuint p : {spawn_, move_, commit_, draw_}) glDeleteProgram(p);
}

void Traffic::bind() const {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, statusBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, worldBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, statsBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, carLane_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, carKin_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, carRoute_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, trafficBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, laneState_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, laneQueue_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, laneReq_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, visible_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, laneStat_);
}

void Traffic::dispatch(GLuint prog, uint32_t items) const {
    if (items == 0) return;
    glUseProgram(prog);
    const GLuint groups = (items + kLocalSize - 1) / kLocalSize;
    const GLuint gx     = std::min<GLuint>(groups, 65535u);
    glDispatchCompute(gx, (groups + gx - 1) / gx, 1);
}
