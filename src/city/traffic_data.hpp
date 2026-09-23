#pragma once
#include "city/mapgen.hpp"

#include <array>
#include <cstdint>
#include <vector>

constexpr float kLocalSpeed    = 8.3f;  // m/s
constexpr float kArterialSpeed = 13.9f; // m/s
constexpr float kCarSlot       = 6.5f;  // m of lane per queued car

// Offsets of the tables inside TrafficWorld::data (in uints). Must match traffic.comp.
enum TrafficSection : int {
    kTSecLanes,       // 8 per lane: from node, to node, queue offset, capacity, length (float bits),
                      //   max speed (float bits), signal, directed edge
                      //   signal: [0,2) phase (0 none, 1 east-west, 2 north-south) | [2,4) lane index
                      //           | [8,16) cycle s | [16,32) offset s
    kTSecDirEdges,    // per directed edge (edge * 2 + dir): first lane | lane count << 24
    kTSecAdjDirEdge,  // per CSR slot: directed edge leaving the node through that slot
    kTSecNodeRegion,  // per node: region
    kTSecNextSlot,    // bytes, region * nodes + node: CSR slot (0..254) towards the region, 255 = none
    kTSectionCount
};

struct TrafficWorld {
    std::vector<uint32_t>                data;
    std::array<uint32_t, kTSectionCount> sec{};
    uint32_t laneCount = 0, queueSize = 0, regionCount = 0, signalCount = 0;
};

// Lanes, signals and region routing tables. Uses all CPU cores for routing.
TrafficWorld buildTrafficWorld(const CityMap& m);
