#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"
#include "city/sim_data.hpp"
#include "city/traffic_data.hpp"

#include "ui/inspector.hpp"

#include <glad/gl.h>
#include <string>

// GPU road traffic: private cars driven by agents plus vans and taxis. Lanes are FIFO queues (traffic.comp).
class Traffic {
public:
    static constexpr float    kStepMs      = 500.0f;
    static constexpr float    kMaxStepMs   = 2000.0f;
    static constexpr int      kMaxSteps    = 8;
    // Car counts follow the population but are capped by lane capacity, otherwise the network gridlocks.
    static constexpr uint32_t kOwnerEvery    = 8;
    static constexpr uint32_t kFleetEvery    = 40;
    static constexpr uint32_t kSlotsPerOwner = 12;
    static constexpr uint32_t kSlotsPerFleet = 60;

    void init(const std::string& shaderDir);
    void setWorld(const CityMap& map, const SimWorld& sim, GLuint worldBuf);
    void reset(uint32_t agentCount, uint32_t seed, GLuint statusBuf, GLuint statsBuf);

    // One step ending at nowMs. advance = false only rebuilds the visible list (paused).
    void step(uint32_t nowMs, float dtS, bool advance, bool emit, bool collect, const Camera& cam, int fbW, int fbH,
              GLuint stride);
    void draw(const Camera& cam, int fbW, int fbH) const;
    void setLaneStats(bool on) { glProgramUniform1ui(move_, 33, on ? 1u : 0u); }
    void drawCongestion(const Camera& cam, int fbW, int fbH, float opacity) const;
    void destroy();

    uint32_t ownerEvery() const { return ownerEvery_; }
    uint32_t ownerCount() const { return ownerCount_; }
    uint32_t fleetCount() const { return carCount_ - ownerCount_; }
    GLuint   carLaneBuffer() const { return carLane_; }
    GLuint   visibleBuffer() const { return visible_; }
    uint32_t visibleCapacity() const { return capacity_; }
    void     setSelectedCar(uint32_t car) { glProgramUniform1ui(move_, 32, car); }
    CarRaw   readCar(uint32_t car) const;                                           // stalls: use rarely
    GLuint   carRouteBuffer() const { return carRoute_; }
    uint32_t carCount() const { return carCount_; }
    const TrafficWorld& world() const { return world_; }

private:
    void bind() const;
    void dispatch(GLuint prog, uint32_t items) const;

    GLuint spawn_ = 0, move_ = 0, commit_ = 0, draw_ = 0, vao_ = 0, congestion_ = 0, laneStat_ = 0;
    uint32_t nodeSec_ = 0;
    GLuint trafficBuf_ = 0, laneState_ = 0, laneQueue_ = 0, laneReq_ = 0;
    GLuint carLane_ = 0, carKin_ = 0, carRoute_ = 0, visible_ = 0;
    GLuint worldBuf_ = 0, statusBuf_ = 0, statsBuf_ = 0;
    uint32_t carCount_ = 0, ownerCount_ = 0, ownerEvery_ = kOwnerEvery, capacity_ = 1, nodeCount_ = 0;
    TrafficWorld world_;
};
