#include "city/generator.hpp"


#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_set>
#include <iterator>
#include <string>
#include <utility>

namespace citygen {


void buildWater(CityMap& m, const Field& f) {
    if (f.bayRadius <= 0.0f) return;
    auto clampToMap = [&](Vec2 p) { return Vec2{std::clamp(p.x, 0.0f, m.size), std::clamp(p.y, 0.0f, m.size)}; };
    const Vec2 c    = f.bayCenter;
    m.water.push_back(clampToMap(c));
    for (int i = 0; i <= 720; ++i) {
        const float a = 2.0f * kPi * float(i) / 720.0f;
        const Vec2  d{std::cos(a), std::sin(a)};
        float       r = 0.0f;
        while (r < 2.5f * f.bayRadius && f.water(c + d * r)) r += 20.0f;
        m.water.push_back(clampToMap(c + d * r));
    }
}


} // namespace citygen

using namespace citygen;


float roadWidth(RoadType t) { return t == RoadType::Arterial ? 16.0f : 7.0f; }

CityPlan planCity(uint32_t population) {
    CityPlan     p;
    const double people = std::max(double(population), 1.0);
    p.population        = population;
    p.size  = std::clamp(float(std::sqrt(people / double(kPlanDensity)) * 1000.0), kPlanMinSize, kPlanMaxSize);
    p.scale = p.size / 20'000.0f;
    p.hubs  = std::clamp(int(std::lround(6.0f * std::sqrt(p.scale))), 3, 9);
    if (population >= 2'000'000u) {
        p.transit = Transit::Metro;
        p.lines   = 2 + std::clamp(int(std::lround(6.0f * p.scale)), 3, 12); // ring, cross-town, radials
    } else if (population >= 500'000u) {
        p.transit = Transit::Commuter;
        p.lines   = std::clamp(int(population / 500'000u), 1, 3);
    }
    // 30 floors at 5M: ~11 at 1M, ~45 at 10M.
    p.towerFloors = std::clamp(30.0f * std::pow(float(people / 5e6), 0.6f), 6.0f, 60.0f);
    return p;
}

CityMap generateCity(uint32_t seed, const CityPlan& plan) {
    const float size = plan.size;
    CityMap     m;
    m.seed = seed;
    m.size = size;
    m.plan = plan;
    Rng rng(seed);

    // Zones (palace, downtowns) shrink with the city, but not to nothing.
    const float zone = std::max(plan.scale, 0.4f);

    Field f;
    f.size      = size;
    f.scale     = plan.scale;
    f.seed      = seed;
    f.bayCenter = {size * rng.uni(0.98f, 1.05f), size * rng.uni(-0.05f, 0.02f)};
    f.bayRadius = size * rng.uni(0.36f, 0.44f);

    const Vec2  C{size * rng.uni(0.42f, 0.50f), size * rng.uni(0.50f, 0.58f)};
    const float R  = size * 0.2f;
    f.palace       = C;
    f.palaceRadius = rng.uni(550.0f, 750.0f) * zone;

    const std::vector<Vec2> hubs = placeHubs(f, rng, C, R, plan.hubs);
    buildRail(f, rng, m, C, R, hubs);

    f.bumps.push_back({C, size * 0.28f, 0.35f});
    for (size_t i = 0; i < hubs.size(); ++i)
        f.bumps.push_back({hubs[i], (i == 0 ? 2000.0f : rng.uni(1100.0f, 1700.0f)) * zone,
                           i == 0 ? 0.95f : rng.uni(0.55f, 0.85f)});
    for (const Station& s : m.stations) {
        f.bumps.push_back({s.pos, 450.0f, 0.22f});
        if (rng.uni() < 0.2f) f.bumps.push_back({s.pos, 900.0f, 0.3f});
    }

    ScalarGrid spacing;
    spacing.build(size, 40.0f, [&](Vec2 p) {
        return lerpf(kMaxSpacing, kMinSpacing, std::pow(f.density(p), 0.8f));
    });

    const Seeded seeded = poissonDisk(f, spacing, planArterials(f, C, R, rng), rng);
    PointGrid    pg;
    pg.build(seeded.pts, size, 120.0f);
    buildStreetGraph(m, seeded.pts, gabriel(seeded.pts, spacing, pg), seeded.arterialPairs, rng);

    PointGrid ng;
    ng.build(m.nodes, size, 150.0f);
    for (Station& s : m.stations) s.node = ng.nearest(m.nodes, s.pos);

    buildDistricts(m, f, C, R, hubs, rng);
    extractBlocks(m, f, rng);
    buildWater(m, f);
    nameStations(m, rng);
    buildBuildings(m, rng);
    return m;
}
