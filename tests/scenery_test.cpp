#include "city/mapgen.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct Rect {
    Vec2  c, ax, ay;
    float hx, hy;
};

Rect footprint(const Building& b) {
    const float along = float(b.tilesAlong) * kBuildingTileMetres, in = float(b.tilesIn) * kBuildingTileMetres;
    return {b.origin + b.axisAlong * (0.5f * along) + b.axisIn * (0.5f * in), b.axisAlong, b.axisIn, 0.5f * along,
            0.5f * in};
}

// Separating axis test, with a little slack for rectangles that only touch.
bool overlaps(const Rect& a, const Rect& b) {
    const Vec2 d = b.c - a.c;
    for (Vec2 l : {a.ax, a.ay, b.ax, b.ay}) {
        const float ra = a.hx * std::abs(dot(a.ax, l)) + a.hy * std::abs(dot(a.ay, l));
        const float rb = b.hx * std::abs(dot(b.ax, l)) + b.hy * std::abs(dot(b.ay, l));
        if (std::abs(dot(d, l)) >= ra + rb - 0.1f) return false;
    }
    return true;
}

bool inside(const Rect& r, Vec2 p) {
    const Vec2 d = p - r.c;
    return std::abs(dot(d, r.ax)) < r.hx && std::abs(dot(d, r.ay)) < r.hy;
}

// Buildings by 100 m cell, for quick neighbourhood queries.
struct Grid {
    float                              cell = 100.0f;
    int                                n    = 1;
    std::vector<std::vector<uint32_t>> items;
    std::vector<Rect>                  rects;

    explicit Grid(const CityMap& m) {
        n = int(m.size / cell) + 1;
        items.assign(size_t(n) * size_t(n), {});
        for (const Building& b : m.buildings) {
            const Rect r = footprint(b);
            const int  x = std::clamp(int(r.c.x / cell), 0, n - 1), y = std::clamp(int(r.c.y / cell), 0, n - 1);
            items[size_t(y) * size_t(n) + size_t(x)].push_back(uint32_t(rects.size()));
            rects.push_back(r);
        }
    }

    template <class F> void near(Vec2 p, F&& fn) const {
        const int x = int(p.x / cell), y = int(p.y / cell);
        for (int yy = std::max(0, y - 1); yy <= std::min(n - 1, y + 1); ++yy)
            for (int xx = std::max(0, x - 1); xx <= std::min(n - 1, x + 1); ++xx)
                for (uint32_t i : items[size_t(yy) * size_t(n) + size_t(xx)]) fn(rects[i]);
    }
};

const CityMap& city() {
    static const CityMap m = generateCity(3, planCity(2'000'000u));
    return m;
}

} // namespace

TEST_CASE("no building stands on a track, a platform or a station square") {
    const CityMap& m = city();
    const Grid     g(m);
    REQUIRE(!m.lines.empty());

    // Along every line, the corridor (a little narrower than the keep-out) is free.
    for (const RailLine& l : m.lines)
        for (size_t i = 0; i + 1 < l.path.size(); ++i) {
            const Vec2  a = l.path[i], b = l.path[i + 1];
            const float len = length(b - a);
            if (len < 1e-3f) continue;
            const Vec2 d = (b - a) / len;
            const Rect corridor{(a + b) * 0.5f, d, perp(d), 0.5f * len, kRailCorridor - 1.0f};
            g.near(corridor.c, [&](const Rect& r) { REQUIRE(!overlaps(corridor, r)); });
        }
    for (const RailStop& s : railStops(m)) {
        const Rect platforms{s.pos, s.dir, perp(s.dir), 0.5f * kPlatformLength, kPlatformHalf};
        g.near(s.pos, [&](const Rect& r) { REQUIRE(!overlaps(platforms, r)); });
    }
    for (const Station& s : m.stations) {
        const Rect plaza{s.pos, {1, 0}, {0, 1}, 0.5f * kPlazaSize, 0.5f * kPlazaSize};
        g.near(s.pos, [&](const Rect& r) { REQUIRE(!overlaps(plaza, r)); });
    }
}

TEST_CASE("every stop is on its line, near its station") {
    const CityMap& m     = city();
    const auto     stops = railStops(m);
    size_t         expected = 0;
    for (const RailLine& l : m.lines) {
        std::vector<uint32_t> s = l.stations;
        std::sort(s.begin(), s.end());
        expected += size_t(std::unique(s.begin(), s.end()) - s.begin());
    }
    REQUIRE(stops.size() == expected);
    for (const RailStop& s : stops) {
        REQUIRE(s.line < m.lines.size());
        REQUIRE(std::abs(length(s.dir) - 1.0f) < 1e-3f);
        REQUIRE(length(s.pos - m.stations[s.station].pos) < 400.0f); // interchanges merge within 350 m
    }
}

TEST_CASE("trees grow in the map, never inside a building") {
    const CityMap& m = city();
    const Grid     g(m);
    REQUIRE(m.trees.size() > 10'000);
    size_t street = 0;
    for (const Tree& t : m.trees) {
        REQUIRE(t.radius > 1.0f);
        REQUIRE(t.radius < 6.0f);
        REQUIRE(t.pos.x >= 0.0f);
        REQUIRE(t.pos.y >= 0.0f);
        REQUIRE(t.pos.x <= m.size);
        REQUIRE(t.pos.y <= m.size);
        street += t.kind == TreeKind::Street;
        if (t.kind == TreeKind::Street) continue; // on the sidewalk, may touch a facade
        g.near(t.pos, [&](const Rect& r) { REQUIRE(!inside(r, t.pos)); });
    }
    REQUIRE(street > 0);
    REQUIRE(street < m.trees.size());
}

TEST_CASE("the palace stands in its park") {
    const CityMap& m = city();
    REQUIRE(m.palaceRadius > 0.0f);
    size_t halls = 0;
    for (const Building& b : m.buildings) halls += length(footprint(b).c - m.palace) < 80.0f;
    REQUIRE(halls >= 3);
}

TEST_CASE("trees are deterministic") {
    const CityMap a = generateCity(9, planCity(600'000u));
    const CityMap b = generateCity(9, planCity(600'000u));
    REQUIRE(a.trees.size() == b.trees.size());
    for (size_t i = 0; i < a.trees.size(); ++i) {
        REQUIRE(a.trees[i].pos.x == b.trees[i].pos.x);
        REQUIRE(a.trees[i].pos.y == b.trees[i].pos.y);
        REQUIRE(a.trees[i].radius == b.trees[i].radius);
    }
}
