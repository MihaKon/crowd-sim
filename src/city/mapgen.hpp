#pragma once
#include "core/math.hpp"

#include <cstdint>
#include <string>
#include <vector>

enum class RoadType : uint8_t { Local, Arterial };
// New values go at the end: the numbers are stored in the simulation tables.
enum class BlockType : uint8_t { Residential, Commercial, Office, Industrial, Park, ResidentialPoor, ResidentialRich };

enum class DistrictKind : uint8_t { Business, Residential, Wealthy, Poor, Industrial };
struct District {
    Vec2         seed;
    DistrictKind kind;
    std::string  name;
};

struct Edge {
    uint32_t a, b;
    RoadType type;
};

struct Station {
    Vec2                  pos;
    uint32_t              node;
    std::vector<uint32_t> lines;
    std::string           name;
};

struct RailLine {
    std::vector<Vec2>     path;
    std::vector<uint32_t> stations;
    bool                  loop      = false;
    bool                  crossTown = false;  // through the centre, between two opposite hubs
    float                 headway   = 240.0f; // s between trains
    std::string           name;
};

// Footprint of one building: a rectangle of tilesAlong x tilesIn roof tiles,
// kBuildingTileMetres each. origin is the (along=0, in=0) corner, on the
// block's street-facing edge; axisIn points into the block.
constexpr float kBuildingTileMetres = 3.5f;
struct Building {
    Vec2    origin;
    Vec2    axisAlong, axisIn;
    uint8_t tilesAlong, tilesIn;
    uint8_t palette; // roof set in the Kenney atlas, see kRoofSet in buildings.vert:
                     // 0 red tiles, 1 orange tiles, 2 concrete, 3 framed concrete, 4 paving, 5 green roof
    uint8_t floors;
    uint8_t facade;  // wall colour, see kFacade in buildings.vert
    uint8_t windows; // window pattern: 0 houses, 1 office bands, 2 shop fronts, 3 industrial
};

struct Block {
    std::vector<Vec2> poly;
    Vec2              centroid;
    float             area;
    BlockType         type;
    uint32_t          anchor;
    uint16_t          district;
    float             floorArea = 0.0f; // m², all floors of all buildings on the block
};

enum class Transit : uint8_t {
    None,     // walking and driving only
    Commuter, // a few surface lines through the centre and out to the suburbs
    Metro,    // ring line, cross-town line and radial lines
};

// Everything about the city that follows from its population. The generator's
// reference distances are tuned for 5M people on 20 km (scale 1).
struct CityPlan {
    uint32_t population  = 0;
    float    size        = 0.0f; // m, side of the square map
    float    scale       = 1.0f; // size / 20 km
    Transit  transit     = Transit::None;
    int      hubs        = 0;     // sub-centres around the centre (rail hubs when there is rail)
    int      lines       = 0;     // rail lines in total
    float    towerFloors = 30.0f; // tallest office towers in the business district
};

constexpr float kPlanDensity = 12'500.0f; // people per km²
constexpr float kPlanMinSize = 4'000.0f;  // m
constexpr float kPlanMaxSize = 28'000.0f; // m, keeps the traffic lanes well under 2^18

struct CityMap {
    uint32_t seed = 0;
    float    size = 0.0f;
    CityPlan plan;

    // Street graph (single connected component), CSR with neighbours sorted CCW.
    std::vector<Vec2>     nodes;
    std::vector<uint32_t> adjOffsets;
    std::vector<uint32_t> adj;
    std::vector<uint32_t> adjEdge;
    std::vector<Edge>     edges;

    std::vector<RailLine> lines;
    std::vector<Station>  stations;
    std::vector<District> districts;
    std::vector<Block>    blocks;
    std::vector<Vec2>     water; // triangle fan: [centre, rim...]; empty if none
    std::vector<Building> buildings;
};

CityPlan planCity(uint32_t population);

float   roadWidth(RoadType t);
// Stages: hubs, rail and stations, density field, streets, districts, blocks, water, names, buildings.
CityMap generateCity(uint32_t seed, const CityPlan& plan);
