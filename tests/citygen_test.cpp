#include "city/mapgen.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {

constexpr float kSize = 20'000.0f;

bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool inMap(Vec2 p, float size) { return p.x >= 0.0f && p.y >= 0.0f && p.x <= size && p.y <= size; }

bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

} // namespace

TEST_CASE("generation is deterministic") {
    const CityMap a = generateCity(42, kSize);
    const CityMap b = generateCity(42, kSize);

    REQUIRE(a.nodes.size() == b.nodes.size());
    for (size_t i = 0; i < a.nodes.size(); ++i) REQUIRE(same(a.nodes[i], b.nodes[i]));
    REQUIRE(a.adj == b.adj);
    REQUIRE(a.edges.size() == b.edges.size());
    REQUIRE(a.blocks.size() == b.blocks.size());
    REQUIRE(a.buildings.size() == b.buildings.size());
    REQUIRE(a.stations.size() == b.stations.size());
    for (size_t i = 0; i < a.stations.size(); ++i) REQUIRE(a.stations[i].name == b.stations[i].name);
}

TEST_CASE("different seeds give different cities") {
    const CityMap a = generateCity(1, kSize);
    const CityMap b = generateCity(2, kSize);
    REQUIRE((a.nodes.size() != b.nodes.size() || !same(a.nodes[0], b.nodes[0])));
}

TEST_CASE("generated city is structurally valid") {
    for (uint32_t seed : {1u, 2u, 3u, 1234u}) {
        CAPTURE(seed);
        const CityMap m = generateCity(seed, kSize);
        const size_t  n = m.nodes.size();

        REQUIRE(m.seed == seed);
        REQUIRE(n > 0);
        REQUIRE(!m.blocks.empty());
        REQUIRE(!m.buildings.empty());

        for (const Vec2& p : m.nodes) {
            REQUIRE(finite(p));
            REQUIRE(inMap(p, kSize));
        }

        for (const Edge& e : m.edges) {
            REQUIRE(e.a < n);
            REQUIRE(e.b < n);
            REQUIRE(e.a != e.b);
            REQUIRE(length2(m.nodes[e.a] - m.nodes[e.b]) > 0.0f);
        }

        REQUIRE(m.adjOffsets.size() == n + 1);
        REQUIRE(m.adjOffsets.front() == 0);
        REQUIRE(m.adjOffsets.back() == m.adj.size());
        REQUIRE(m.adj.size() == m.adjEdge.size());
        REQUIRE(m.adj.size() == 2 * m.edges.size());
        for (size_t v = 0; v < n; ++v) {
            REQUIRE(m.adjOffsets[v] <= m.adjOffsets[v + 1]);
            for (uint32_t k = m.adjOffsets[v]; k < m.adjOffsets[v + 1]; ++k) {
                const Edge& e = m.edges[m.adjEdge[k]];
                REQUIRE(((e.a == v && e.b == m.adj[k]) || (e.b == v && e.a == m.adj[k])));
            }
        }

        std::vector<char>     seen(n, 0);
        std::vector<uint32_t> stack{0};
        seen[0]      = 1;
        size_t count = 1;
        while (!stack.empty()) {
            const uint32_t v = stack.back();
            stack.pop_back();
            for (uint32_t k = m.adjOffsets[v]; k < m.adjOffsets[v + 1]; ++k)
                if (!seen[m.adj[k]]) {
                    seen[m.adj[k]] = 1;
                    ++count;
                    stack.push_back(m.adj[k]);
                }
        }
        REQUIRE(count == n);

        for (const Station& s : m.stations) {
            REQUIRE(s.node < n);
            REQUIRE(finite(s.pos));
        }
        for (const RailLine& l : m.lines) {
            for (uint32_t s : l.stations) REQUIRE(s < m.stations.size());
        }

        for (const Block& b : m.blocks) {
            REQUIRE(b.poly.size() >= 3);
            REQUIRE(b.district < m.districts.size());
            REQUIRE(b.anchor < n);
            REQUIRE(b.area > 0.0f);
            REQUIRE(finite(b.centroid));
        }

        for (const Building& b : m.buildings) {
            REQUIRE(finite(b.origin));
            REQUIRE(b.tilesAlong > 0);
            REQUIRE(b.tilesIn > 0);
            REQUIRE(b.floors > 0);
            REQUIRE(b.palette <= 5);
            REQUIRE(b.windows <= 3);
        }
    }
}

TEST_CASE("roadWidth: arterials are wider than local roads") {
    REQUIRE(roadWidth(RoadType::Arterial) > roadWidth(RoadType::Local));
    REQUIRE(roadWidth(RoadType::Local) > 0.0f);
}
