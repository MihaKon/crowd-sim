#pragma once
#include "city/mapgen.hpp"
#include "city/sim_data.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Raw GPU state of one agent / car (see agents.comp and traffic.comp).
struct AgentRaw {
    uint32_t id = 0, ev = 0, state = 0;
    uint32_t leg[4]{}, plan[2]{};
};
struct CarRaw {
    uint32_t car = 0, lane = 0;
    float    kin[4]{};  // position on lane, speed, time stopped, transfers
    uint32_t route[4]{}; // destination, parked-at node, driver, next departure
};

struct InspectContext {
    const CityMap&  map;
    const SimWorld& sim;
    uint32_t        seed, dayOffsetMs, nowMs, ownerEvery, ownerCount;
};

struct Panel {
    std::string                                      title, subtitle;
    std::vector<std::pair<std::string, std::string>> rows;
};

// Name, age, job, class and income are derived from the agent id, so they need no storage.
Panel describeAgent(const InspectContext& ctx, const AgentRaw& a, const CarRaw* car);
Panel describeCar(const InspectContext& ctx, const CarRaw& c);

// Leg kind of an agent state (K_CAR = 4: leg[0] is the car).
inline uint32_t agentLegKind(uint32_t state) { return (state >> 4) & 7u; }
constexpr uint32_t kAgentInCar = 4;
