#pragma once
#include "city/mapgen.hpp"

#include <array>
#include <cstdint>
#include <vector>

constexpr float kTrainSpeed   = 16.7f;  // m/s
constexpr float kRailDwell    = 30.0f;  // s
constexpr float kTransferTime = 240.0f; // s
constexpr float kLoopHeadway  = 180.0f; // s, ring and cross-town lines
constexpr float kLineHeadway  = 240.0f; // s, radial lines

// Offsets of the tables inside SimWorld::data (in uints). Must match common.glsl.
enum Section : int {
    kSecNodePos,      // 2 per node: x, y (float bits)
    kSecAdjOff,       // CSR offsets, nodes + 1
    kSecAdj,          // CSR neighbours; top bit set: the street to it is an arterial
    kSecBlocks,       // 4 per block: anchor node, nearest station, shop block, type
    kSecPick,         // residential block ids, then workplaces by pay: low, mid, high
                      // (weighted by repetition: bigger employers appear more often)
    kSecRailPts,      // 4 per point: x, y, arc length (float bits), 0
    kSecLineInfo,     // 4 per line: first point, point count, loop, total length (float bits)
    kSecStationInfo,  // 4 per station: node, x, y (float bits), 0
    kSecRailTable,    // 2 per (from, to) station: travel time s (float bits), first leg (line << 16 | alight)
    kSecLineDir,      // 8 per line direction (line * 2 + dir):
                      //   first profile entry, entries, loop, line,
                      //   headway ms, period ms (loop) / trip duration ms, fleet (loop) / trips per day, occupancy base
    kSecProfile,      // 4 per entry: station, arc (float bits), arrival ms, departure ms (from the first station)
    kSecProfileIndex, // line directions * stations: entry of the station in the profile, or kNone
    kSectionCount
};

// Everything the agent kernel needs, packed into one uint array (one SSBO).
struct SimWorld {
    std::vector<uint32_t>               data;
    std::array<uint32_t, kSectionCount> sec{};
    uint32_t residentialCount = 0, workCount = 0, stationCount = 0, lineCount = 0;
    uint32_t workByPay[3] = {0, 0, 0};
    uint32_t occupancyCount = 0;
    std::vector<uint32_t> trainSlots;
};

SimWorld buildSimWorld(const CityMap& m);
