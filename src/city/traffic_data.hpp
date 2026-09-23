#pragma once
#include "city/mapgen.hpp"
#include "gpu_layout.h"

#include <array>
#include <cstdint>
#include <vector>

constexpr float kLocalSpeed    = 8.3f;  // m/s
constexpr float kArterialSpeed = 13.9f; // m/s
constexpr float kCarSlot       = 6.5f;  // m of lane per queued car

// Offsets of the tables inside TrafficWorld::data (in uints). Indices come from gpu_layout.h (shared with GLSL).
enum TrafficSection : int {
    kTSecLanes       = TSEC_LANES,        // 8 per lane: from node, to node, queue offset, capacity, length (float bits),
                                          //   max speed (float bits), signal, directed edge
                                          //   signal: [0,2) phase (0 none, 1 east-west, 2 north-south) | [2,4) lane index
                                          //           | [8,16) cycle s | [16,32) offset s
    kTSecDirEdges    = TSEC_DIR_EDGES,    // per directed edge (edge * 2 + dir): first lane | lane count << 24
    kTSecAdjDirEdge  = TSEC_ADJ_DIR_EDGE, // per CSR slot: directed edge leaving the node through that slot
    kTSecNodeRegion  = TSEC_NODE_REGION,  // per node: region
    kTSecNextSlot    = TSEC_NEXT_SLOT,    // bytes, region * nodes + node: CSR slot (0..254) towards the region, 255 = none
    kTSectionCount   = TRAFFIC_SECTION_COUNT
};

struct TrafficWorld {
    std::vector<uint32_t>                data;
    std::array<uint32_t, kTSectionCount> sec{};
    uint32_t laneCount = 0, queueSize = 0, regionCount = 0, signalCount = 0;
};

// Lanes, signals and region routing tables. Uses all CPU cores for routing.
TrafficWorld buildTrafficWorld(const CityMap& m);
