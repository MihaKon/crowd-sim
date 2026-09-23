#include "city/traffic_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <thread>

namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;

uint32_t bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

uint32_t hashU(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float edgeTime(const CityMap& m, uint32_t e) {
    const Edge& ed  = m.edges[e];
    const float len = length(m.nodes[ed.b] - m.nodes[ed.a]);
    return len / (ed.type == RoadType::Arterial ? kArterialSpeed : kLocalSpeed) + 2.0f; // + 2 s per junction
}

// Shortest-time tree towards `root`: for every node, the CSR slot of the next node on the way.
void routeTree(const CityMap& m, uint32_t root, uint8_t* out, std::vector<float>& dist,
               std::vector<uint32_t>& parent) {
    const size_t n = m.nodes.size();
    std::fill(dist.begin(), dist.end(), std::numeric_limits<float>::infinity());
    std::fill(parent.begin(), parent.end(), kNone);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    dist[root] = 0.0f;
    pq.push({0.0f, root});
    while (!pq.empty()) {
        const auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[u]) continue;
        for (uint32_t k = m.adjOffsets[u]; k < m.adjOffsets[u + 1]; ++k) {
            const uint32_t v = m.adj[k];
            const float    c = d + edgeTime(m, m.adjEdge[k]);
            if (c < dist[v]) {
                dist[v]   = c;
                parent[v] = u;
                pq.push({c, v});
            }
        }
    }
    for (uint32_t v = 0; v < n; ++v) {
        out[v] = 255;
        if (parent[v] == kNone) continue;
        for (uint32_t k = m.adjOffsets[v]; k < m.adjOffsets[v + 1]; ++k)
            if (m.adj[k] == parent[v]) {
                out[v] = uint8_t(std::min<uint32_t>(k - m.adjOffsets[v], 254));
                break;
            }
    }
}

} // namespace

TrafficWorld buildTrafficWorld(const CityMap& m) {
    const uint32_t N = uint32_t(m.nodes.size()), E = uint32_t(m.edges.size());
    TrafficWorld   w;

    std::vector<uint8_t> arterialDeg(N, 0);
    for (const Edge& e : m.edges)
        if (e.type == RoadType::Arterial) {
            ++arterialDeg[e.a];
            ++arterialDeg[e.b];
        }

    std::vector<uint32_t> lanes, dirEdges(size_t(E) * 2);
    std::vector<uint8_t>  signalled(N, 0);
    for (uint32_t e = 0; e < E; ++e) {
        const Edge& ed  = m.edges[e];
        const bool  art = ed.type == RoadType::Arterial;
        for (uint32_t d = 0; d < 2; ++d) {
            const uint32_t from = d ? ed.b : ed.a, to = d ? ed.a : ed.b;
            const Vec2     dv   = m.nodes[to] - m.nodes[from];
            const float    len  = length(dv);
            const uint32_t cnt  = art ? 2 : 1;
            dirEdges[size_t(e) * 2 + d] = w.laneCount | (cnt << 24);

            // Signals only where 3+ arterials meet; side streets merge into gaps.
            uint32_t signal = 0;
            if (arterialDeg[to] >= 3) {
                const uint32_t cycle = art ? 90 : 60;
                const uint32_t phase = std::abs(dv.x) >= std::abs(dv.y) ? 1 : 2;
                signal               = phase | (cycle << 8) | ((hashU(to) % cycle) << 16);
                signalled[to]        = 1;
            }
            for (uint32_t k = 0; k < cnt; ++k) {
                const uint32_t cap = std::max(1u, uint32_t(len / kCarSlot));
                lanes.insert(lanes.end(), {from, to, w.queueSize, cap, bits(len),
                                           bits(art ? kArterialSpeed : kLocalSpeed), signal | (k << 2), e * 2 + d});
                w.queueSize += cap;
                ++w.laneCount;
            }
        }
    }
    for (uint8_t s : signalled) w.signalCount += s;
    if (w.laneCount >= (1u << 18)) throw std::runtime_error("too many lanes for the request encoding (2^18)");

    std::vector<uint32_t> adjDirEdge(m.adj.size());
    for (uint32_t v = 0; v < N; ++v)
        for (uint32_t k = m.adjOffsets[v]; k < m.adjOffsets[v + 1]; ++k) {
            const uint32_t e = m.adjEdge[k];
            adjDirEdge[k]    = e * 2 + (m.edges[e].a == v ? 0 : 1);
        }

    const uint32_t        want = std::clamp<uint32_t>(N / 40, 16, 1024);
    const int             g    = int(std::ceil(std::sqrt(double(want))));
    std::vector<uint32_t> centres;
    for (int y = 0; y < g; ++y)
        for (int x = 0; x < g; ++x) {
            const Vec2 c{(float(x) + 0.5f) * m.size / float(g), (float(y) + 0.5f) * m.size / float(g)};
            uint32_t   best  = kNone;
            float      bestD = (m.size / float(g)) * (m.size / float(g)) * 0.5f;
            for (uint32_t v = 0; v < N; ++v) {
                const float d = length2(m.nodes[v] - c);
                if (d < bestD) {
                    bestD = d;
                    best  = v;
                }
            }
            if (best != kNone) centres.push_back(best);
        }
    w.regionCount = uint32_t(centres.size());

    std::vector<uint32_t> nodeRegion(N, 0);
    {
        std::vector<float> dist(N, std::numeric_limits<float>::infinity());
        using Item = std::pair<float, uint32_t>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        for (uint32_t r = 0; r < centres.size(); ++r) {
            dist[centres[r]]       = 0.0f;
            nodeRegion[centres[r]] = r;
            pq.push({0.0f, centres[r]});
        }
        while (!pq.empty()) {
            const auto [d, u] = pq.top();
            pq.pop();
            if (d > dist[u]) continue;
            for (uint32_t k = m.adjOffsets[u]; k < m.adjOffsets[u + 1]; ++k) {
                const uint32_t v = m.adj[k];
                const float    c = d + edgeTime(m, m.adjEdge[k]);
                if (c < dist[v]) {
                    dist[v]       = c;
                    nodeRegion[v] = nodeRegion[u];
                    pq.push({c, v});
                }
            }
        }
    }

    std::vector<uint8_t> nextSlot(size_t(w.regionCount) * N + 3, 255);
    {
        const unsigned           threads = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t)
            pool.emplace_back([&, t] {
                std::vector<float>    dist(N);
                std::vector<uint32_t> parent(N);
                for (uint32_t r = t; r < w.regionCount; r += threads)
                    routeTree(m, centres[r], &nextSlot[size_t(r) * N], dist, parent);
            });
        for (auto& th : pool) th.join();
    }

    auto& d     = w.data;
    auto  begin = [&](TrafficSection s) { w.sec[size_t(s)] = uint32_t(d.size()); };
    begin(kTSecLanes);
    d.insert(d.end(), lanes.begin(), lanes.end());
    begin(kTSecDirEdges);
    d.insert(d.end(), dirEdges.begin(), dirEdges.end());
    begin(kTSecAdjDirEdge);
    d.insert(d.end(), adjDirEdge.begin(), adjDirEdge.end());
    begin(kTSecNodeRegion);
    d.insert(d.end(), nodeRegion.begin(), nodeRegion.end());
    begin(kTSecNextSlot);
    const size_t words = nextSlot.size() / 4;
    const size_t at    = d.size();
    d.resize(at + words);
    std::memcpy(&d[at], nextSlot.data(), words * 4); // little endian: byte i of word = slot 4k+i
    return w;
}
