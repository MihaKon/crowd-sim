#include "city/mapgen.hpp"
#include "city/sim_data.hpp"
#include "city/traffic_data.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

int tallest(const CityMap& m) {
    int f = 0;
    for (const Building& b : m.buildings) f = std::max(f, int(b.floors));
    return f;
}

} // namespace

TEST_CASE("planCity: 5M people is the reference city") {
    const CityPlan p = planCity(5'000'000u);
    REQUIRE(p.size == 20'000.0f);
    REQUIRE(p.scale == 1.0f);
    REQUIRE(p.transit == Transit::Metro);
    REQUIRE(p.hubs == 6);
    REQUIRE(p.lines == 8);
    REQUIRE(p.towerFloors == 30.0f);
}

TEST_CASE("planCity: the city grows with its population") {
    float size = 0.0f, floors = 0.0f;
    for (uint32_t people : {100'000u, 500'000u, 1'000'000u, 2'000'000u, 5'000'000u, 9'000'000u, 20'000'000u}) {
        CAPTURE(people);
        const CityPlan p = planCity(people);
        REQUIRE(p.population == people);
        REQUIRE(p.size >= size);
        REQUIRE(p.towerFloors >= floors);
        REQUIRE(p.size >= kPlanMinSize);
        REQUIRE(p.size <= kPlanMaxSize);
        REQUIRE(p.scale == p.size / 20'000.0f);
        if (p.size > kPlanMinSize && p.size < kPlanMaxSize) {
            const float density = float(people) / (p.size * p.size * 1e-6f); // per km²
            REQUIRE(std::abs(density - kPlanDensity) < 1.0f);
        }
        size   = p.size;
        floors = p.towerFloors;
    }
    REQUIRE(planCity(0).size == kPlanMinSize);
    REQUIRE(planCity(4'000'000'000u).size == kPlanMaxSize);
}

TEST_CASE("planCity: transit by population") {
    REQUIRE(planCity(499'999u).transit == Transit::None);
    REQUIRE(planCity(499'999u).lines == 0);
    REQUIRE(planCity(500'000u).transit == Transit::Commuter);
    REQUIRE(planCity(500'000u).lines == 1);
    REQUIRE(planCity(1'999'999u).transit == Transit::Commuter);
    REQUIRE(planCity(1'999'999u).lines == 3);
    REQUIRE(planCity(2'000'000u).transit == Transit::Metro);
    REQUIRE(planCity(10'000'000u).lines > planCity(2'000'000u).lines);
}

TEST_CASE("a small town has no rail and still gets sim tables") {
    const CityMap m = generateCity(3, planCity(300'000u));
    REQUIRE(m.lines.empty());
    REQUIRE(m.stations.empty());
    REQUIRE(!m.buildings.empty());
    REQUIRE(tallest(m) <= int(std::lround(m.plan.towerFloors)));

    const SimWorld w = buildSimWorld(m);
    REQUIRE(w.stationCount == 0);
    REQUIRE(w.lineCount == 0);
    REQUIRE(w.trainSlots.empty());
    REQUIRE(w.residentialCount > 0);
    for (uint32_t n : w.workByPay) REQUIRE(n > 0);
}

TEST_CASE("a mid-size city has commuter rail, no ring line") {
    const CityMap m = generateCity(3, planCity(1'500'000u));
    REQUIRE(!m.lines.empty());
    REQUIRE(int(m.lines.size()) <= m.plan.lines);
    REQUIRE(m.lines[0].crossTown);
    for (const RailLine& l : m.lines) {
        REQUIRE(!l.loop);
        REQUIRE(l.headway > 240.0f);
    }
    REQUIRE(tallest(m) <= int(std::lround(m.plan.towerFloors)));
    const SimWorld w = buildSimWorld(m);
    REQUIRE(!w.trainSlots.empty());
}

TEST_CASE("every commuter line has an interchange with the cross-town line") {
    for (uint32_t seed : {1u, 3u, 7u}) {
        CAPTURE(seed);
        const CityMap m = generateCity(seed, planCity(1'500'000u));
        REQUIRE(m.lines.size() >= 2);
        const auto& cross = m.lines[0].stations;
        for (size_t l = 1; l < m.lines.size(); ++l) {
            const auto& st = m.lines[l].stations;
            REQUIRE(std::any_of(st.begin(), st.end(),
                                [&](uint32_t s) { return std::find(cross.begin(), cross.end(), s) != cross.end(); }));
        }
    }
}

TEST_CASE("a big city has a metro with a ring line") {
    const CityMap m = generateCity(3, planCity(2'000'000u));
    REQUIRE(m.lines.size() >= 3);
    REQUIRE(m.lines[0].loop);
    REQUIRE(m.lines[1].crossTown);
    REQUIRE(std::any_of(m.lines.begin(), m.lines.end(), [](const RailLine& l) { return l.headway == 180.0f; }));
    // Towers are taller than anything a 300k town allows.
    REQUIRE(tallest(m) > int(std::lround(planCity(300'000u).towerFloors)));
}

TEST_CASE("floor area adds up the buildings on each block") {
    const CityMap m = generateCity(5, planCity(1'000'000u));
    double        blocks = 0.0, buildings = 0.0;
    for (const Block& b : m.blocks) {
        REQUIRE(b.floorArea >= 0.0f);
        if (b.type == BlockType::Park) REQUIRE(b.floorArea == 0.0f);
        blocks += b.floorArea;
    }
    for (const Building& b : m.buildings) {
        if (length(b.origin - m.palace) < 0.3f * m.palaceRadius) continue; // the palace: no homes or jobs
        buildings += double(b.tilesAlong) * b.tilesIn * kBuildingTileMetres * kBuildingTileMetres * b.floors;
    }
    REQUIRE(blocks > 0.0);
    REQUIRE(std::abs(blocks - buildings) / buildings < 1e-4);
}

TEST_CASE("the largest city stays under the traffic lane limit") {
    const CityMap      m = generateCity(2, planCity(20'000'000u));
    const TrafficWorld w = buildTrafficWorld(m);
    REQUIRE(m.size == kPlanMaxSize);
    REQUIRE(w.laneCount < (1u << 18) * 9 / 10);
}

TEST_CASE("homes keep the class mix and favour blocks with more floor area") {
    const CityMap  m = generateCity(1, planCity(1'000'000u));
    const SimWorld w = buildSimWorld(m);
    const uint32_t* pick = w.data.data() + w.sec[kSecPick];

    double              byClass[3] = {0, 0, 0};
    std::vector<double> perBlock(m.blocks.size(), 0.0);
    for (uint32_t i = 0; i < w.residentialCount; ++i) {
        const BlockType t = m.blocks[pick[i]].type;
        byClass[t == BlockType::ResidentialPoor ? 0 : t == BlockType::ResidentialRich ? 2 : 1] += 1.0;
        perBlock[pick[i]] += 1.0;
    }
    const double total = byClass[0] + byClass[1] + byClass[2];
    REQUIRE(std::abs(byClass[0] / total - 0.21) < 0.02);
    REQUIRE(std::abs(byClass[1] / total - 0.67) < 0.02);
    REQUIRE(std::abs(byClass[2] / total - 0.12) < 0.02);

    // Within a class, entries follow floor area: compare the biggest and a median block.
    std::vector<uint32_t> ordinary;
    for (uint32_t b = 0; b < m.blocks.size(); ++b)
        if (m.blocks[b].type == BlockType::Residential && m.blocks[b].floorArea > 0.0f) ordinary.push_back(b);
    REQUIRE(ordinary.size() > 10);
    std::sort(ordinary.begin(), ordinary.end(),
              [&](uint32_t a, uint32_t b) { return m.blocks[a].floorArea < m.blocks[b].floorArea; });
    const uint32_t big = ordinary.back(), median = ordinary[ordinary.size() / 2];
    const double   areaRatio  = double(m.blocks[big].floorArea) / double(m.blocks[median].floorArea);
    const double   entryRatio = perBlock[big] / perBlock[median];
    REQUIRE(entryRatio > 0.8 * areaRatio);
    REQUIRE(entryRatio < 1.25 * areaRatio);
}

