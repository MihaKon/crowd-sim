#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"
#include "city/sim_data.hpp"

#include "ui/inspector.hpp"

#include <glad/gl.h>
#include <string>

// GPU agents with daily schedules and the trains they ride (shaders/agents.comp, common.glsl).
class Agents {
public:
    struct Stats {
        uint32_t walking = 0, waiting = 0, riding = 0, home = 0, work = 0, shop = 0;
        uint32_t leftBehind = 0;
        uint32_t driving = 0, carsOnRoad = 0, carsStopped = 0;
        uint32_t outdoor() const { return walking + waiting; }
    };

    void init(const std::string& shaderDir);
    void setWorld(const CityMap& map);
    void reset(GLuint count, GLuint seed);

    // Advances to nowMs and collects every stride-th outdoor agent in view for drawing.
    // timings (optional) receives {sim, cull} ms, measured with glFinish: stalls, use rarely.
    void update(uint32_t nowMs, bool simulate, const Camera& cam, int fbW, int fbH, GLuint stride,
                bool collectStats, double* timings);
    void draw(const Camera& cam, int fbW, int fbH, float pointSize) const;
    void drawTrains(const Camera& cam, int fbW, int fbH, uint32_t nowMs) const;
    Stats readStats() const;

    void destroy();

    void setCarBuffers(GLuint lane, GLuint route) { carLane_ = lane; carRoute_ = route; }
    void setCarOwnership(uint32_t every) { glProgramUniform1ui(sim_, 23, every); }
    GLuint worldBuffer() const { return worldBuf_; }
    GLuint visibleBuffer() const { return visible_; }
    GLuint visibleCapacity() const { return capacity_; }
    void   setSelected(uint32_t id) { glProgramUniform1ui(cull_, 24, id); }
    void   setHeat(uint32_t mode, uint32_t dim, float cellsPerMetre) {
        glProgramUniform1ui(cull_, 25, mode);
        glProgramUniform1ui(cull_, 26, dim);
        glProgramUniform1f(cull_, 27, cellsPerMetre);
    }
    AgentRaw readAgent(uint32_t id) const;                                   // stalls: use rarely
    GLuint statusBuffer() const { return status_; }
    GLuint statsBuffer() const { return stats_; }

    GLuint   count() const { return count_; }
    GLuint   trainCapacity() const { return trainCap_; }
    uint32_t dayOffsetMs() const { return kDayOffset; }
    const SimWorld& world() const { return world_; }

    static constexpr uint32_t kDayOffset = 6u * 3600u * 1000u; // simulation starts at 06:00

private:
    void bind() const;
    void dispatch(GLuint perThread = 1) const;
    void run(int mode, uint32_t nowMs, bool simulate, const float view[4], GLuint stride, bool collectStats,
             double* timings);

    GLuint   sim_ = 0, cull_ = 0, draw_ = 0, vao_ = 0;
    GLuint   trainProg_ = 0, trainVao_ = 0, trainSlots_ = 0;
    GLuint   status_ = 0, legs_ = 0, plans_ = 0, cells_ = 0, worldBuf_ = 0, visible_ = 0, stats_ = 0, occupancy_ = 0;
    GLuint   count_ = 0, capacity_ = 0, seed_ = 0, trainCap_ = 1, slotCount_ = 0;
    uint32_t occupancyDay_ = 0;
    GLuint   carLane_ = 0, carRoute_ = 0;
    SimWorld world_;
};
