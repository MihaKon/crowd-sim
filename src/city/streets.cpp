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


std::vector<std::vector<Vec2>> planArterials(const Field& f, Vec2 C, float R, Rng& rng) {
    std::vector<std::vector<Vec2>> out;
    auto usable = [&](Vec2 p) { return inside(p, f.size) && !f.water(p); };

    const int   nRad = 10 + rng.below(4);
    const float rot  = rng.uni(0.0f, 2.0f * kPi);
    for (int i = 0; i < nRad; ++i) {
        const float a   = rot + 2.0f * kPi * float(i) / float(nRad) + rng.uni(-0.15f, 0.15f);
        const Vec2  dir = {std::cos(a), std::sin(a)};
        Vec2        p = C + dir * (f.palaceRadius + 200.0f), h = dir;
        std::vector<Vec2> line;
        while (usable(p) && line.size() < 1000) {
            line.push_back(p);
            h = normalize(rotate(h, rng.normal(0.03f)) * 0.9f + dir * 0.1f);
            p = p + h * 60.0f;
        }
        if (line.size() > 10) out.push_back(std::move(line));
    }

    for (float rr : {0.55f, 1.35f, 2.2f}) {
        const float    radius = R * rr;
        const float    a0     = rng.uni(0.0f, 2.0f * kPi);
        const uint32_t ns     = uint32_t(f.seed + uint32_t(rr * 1000.0f));
        const int      steps  = int(2.0f * kPi * radius / 60.0f);
        std::vector<Vec2> line;
        bool closed = true;
        for (int k = 0; k <= steps; ++k) {
            const float a = a0 + 2.0f * kPi * float(k) / float(steps);
            const float n = fbm(Vec2{std::cos(a), std::sin(a)} * 2.0f, ns) - 0.5f;
            const Vec2  p = C + Vec2{std::cos(a), std::sin(a)} * (radius * (1.0f + 0.25f * n));
            if (usable(p)) {
                line.push_back(p);
            } else {
                closed = false;
                if (line.size() > 10) out.push_back(line);
                line.clear();
            }
        }
        if (closed && !line.empty()) line.back() = line.front();
        if (line.size() > 10) out.push_back(std::move(line));
    }
    return out;
}


// Variable-radius Poisson disk. Arterial polylines are inserted first, so
// consecutive arterial points end up as Gabriel neighbours (connected streets).
Seeded poissonDisk(const Field& f, const ScalarGrid& spacing, const std::vector<std::vector<Vec2>>& arterials,
                   Rng& rng) {
    // Minimum accepted distance is 0.75 * kMinSpacing -> at most one point per cell.
    const float cell = 0.75f * kMinSpacing / std::sqrt(2.0f);
    const int   gn   = int(std::ceil(f.size / cell));

    std::vector<int32_t>  grid(size_t(gn) * size_t(gn), -1);
    Seeded                out;
    auto&                 pts = out.pts;
    std::vector<uint32_t> active;

    auto cx = [&](float v) { return std::clamp(int(v / cell), 0, gn - 1); };

    auto nearestWithin = [&](Vec2 q, float r) {
        const int k = int(std::ceil(r / cell));
        const int x = cx(q.x), y = cx(q.y);
        int32_t   best  = -1;
        float     bestD = r * r;
        for (int yy = std::max(0, y - k); yy <= std::min(gn - 1, y + k); ++yy)
            for (int xx = std::max(0, x - k); xx <= std::min(gn - 1, x + k); ++xx) {
                const int32_t i = grid[size_t(yy) * size_t(gn) + size_t(xx)];
                if (i < 0) continue;
                const float d = length2(pts[size_t(i)] - q);
                if (d < bestD) {
                    bestD = d;
                    best  = i;
                }
            }
        return best;
    };

    auto add = [&](Vec2 p) {
        grid[size_t(cx(p.y)) * size_t(gn) + size_t(cx(p.x))] = int32_t(pts.size());
        active.push_back(uint32_t(pts.size()));
        pts.push_back(p);
        return uint32_t(pts.size() - 1);
    };

    for (const auto& line : arterials) {
        uint32_t prev = kNone;
        float    acc = 0.0f, next = 0.0f;
        for (size_t i = 0; i < line.size(); ++i) {
            if (i > 0) acc += length(line[i] - line[i - 1]);
            if (i > 0 && acc < next && i + 1 < line.size()) continue;
            const Vec2    q    = line[i];
            const float   sp   = spacing.sample(q);
            const int32_t near = nearestWithin(q, 0.75f * sp);
            const uint32_t cur = near >= 0 ? uint32_t(near) : add(q);
            if (prev != kNone && prev != cur) out.arterialPairs.emplace_back(std::min(prev, cur), std::max(prev, cur));
            prev = cur;
            acc  = 0.0f;
            next = 0.8f * sp;
        }
    }

    for (int i = 0; i < 64; ++i) {
        const Vec2 p{rng.uni(0.0f, f.size), rng.uni(0.0f, f.size)};
        if (!f.water(p) && nearestWithin(p, spacing.sample(p)) < 0) add(p);
    }

    constexpr int kTries = 16;
    while (!active.empty()) {
        const size_t ai     = size_t(rng.below(int(active.size())));
        const Vec2   p      = pts[active[ai]];
        const float  r      = spacing.sample(p);
        bool         placed = false;
        for (int t = 0; t < kTries; ++t) {
            const float ang = rng.uni(0.0f, 2.0f * kPi);
            const Vec2  q   = p + Vec2{std::cos(ang), std::sin(ang)} * (r * rng.uni(1.0f, 2.0f));
            if (q.x < 0.0f || q.y < 0.0f || q.x >= f.size || q.y >= f.size || f.water(q)) continue;
            if (nearestWithin(q, spacing.sample(q)) < 0) {
                add(q);
                placed = true;
                break;
            }
        }
        if (!placed) {
            active[ai] = active.back();
            active.pop_back();
        }
    }
    return out;
}

// Gabriel graph: edge pq exists iff the circle with diameter pq is empty.
std::vector<std::pair<uint32_t, uint32_t>> gabriel(const std::vector<Vec2>& pts, const ScalarGrid& spacing,
                                                    const PointGrid& pg) {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (uint32_t i = 0; i < pts.size(); ++i) {
        const float Ri = 2.5f * spacing.sample(pts[i]);
        pg.query(pts[i], Ri, [&](uint32_t j) {
            if (j == i) return;
            const float d2 = length2(pts[j] - pts[i]);
            if (d2 >= Ri * Ri) return;
            const float Rj = 2.5f * spacing.sample(pts[j]);
            if (j < i && d2 < Rj * Rj) return; // already considered from j

            const Vec2  mid   = (pts[i] + pts[j]) * 0.5f;
            const float r2    = d2 * 0.25f;
            bool        empty = true;
            pg.query(mid, std::sqrt(r2), [&](uint32_t k) {
                if (k != i && k != j && length2(pts[k] - mid) < r2 * 0.999f) empty = false;
            });
            if (empty) out.emplace_back(std::min(i, j), std::max(i, j));
        });
    }
    return out;
}

namespace {

void buildCsr(CityMap& m) {
    const size_t n = m.nodes.size();
    m.adjOffsets.assign(n + 1, 0);
    for (const Edge& e : m.edges) {
        ++m.adjOffsets[e.a + 1];
        ++m.adjOffsets[e.b + 1];
    }
    for (size_t i = 1; i <= n; ++i) m.adjOffsets[i] += m.adjOffsets[i - 1];
    m.adj.resize(m.edges.size() * 2);
    m.adjEdge.resize(m.edges.size() * 2);

    std::vector<uint32_t> cur(m.adjOffsets.begin(), m.adjOffsets.end() - 1);
    for (uint32_t i = 0; i < m.edges.size(); ++i) {
        const Edge& e       = m.edges[i];
        m.adj[cur[e.a]]     = e.b;
        m.adjEdge[cur[e.a]++] = i;
        m.adj[cur[e.b]]     = e.a;
        m.adjEdge[cur[e.b]++] = i;
    }

    // Sort each neighbour list counter-clockwise (needed for face extraction).
    struct Slot {
        float    ang;
        uint32_t node, edge;
    };
    std::vector<Slot> tmp;
    for (uint32_t u = 0; u < n; ++u) {
        tmp.clear();
        for (uint32_t s = m.adjOffsets[u]; s < m.adjOffsets[u + 1]; ++s) {
            const Vec2 d = m.nodes[m.adj[s]] - m.nodes[u];
            tmp.push_back({std::atan2(d.y, d.x), m.adj[s], m.adjEdge[s]});
        }
        std::sort(tmp.begin(), tmp.end(), [](const Slot& x, const Slot& y) { return x.ang < y.ang; });
        for (size_t k = 0; k < tmp.size(); ++k) {
            m.adj[m.adjOffsets[u] + k]     = tmp[k].node;
            m.adjEdge[m.adjOffsets[u] + k] = tmp[k].edge;
        }
    }
}

uint64_t pairKey(uint32_t a, uint32_t b) {
    return (uint64_t(std::min(a, b)) << 32) | uint64_t(std::max(a, b));
}

} // namespace

void buildStreetGraph(CityMap& m, const std::vector<Vec2>& pts,
                      const std::vector<std::pair<uint32_t, uint32_t>>& pairs,
                      const std::vector<std::pair<uint32_t, uint32_t>>& arterialPairs, Rng& rng) {
    const uint32_t                     n = uint32_t(pts.size());
    std::vector<std::vector<uint32_t>> nb(n);
    for (auto [a, b] : pairs) {
        nb[a].push_back(b);
        nb[b].push_back(a);
    }
    std::unordered_set<uint64_t> arterial;
    for (auto [a, b] : arterialPairs) arterial.insert(pairKey(a, b));

    auto hasEdge = [&](uint32_t a, uint32_t b) { return std::find(nb[a].begin(), nb[a].end(), b) != nb[a].end(); };
    auto removeEdge = [&](uint32_t a, uint32_t b) {
        if (arterial.count(pairKey(a, b))) return false;
        auto erase = [](std::vector<uint32_t>& v, uint32_t x) { v.erase(std::find(v.begin(), v.end(), x)); };
        erase(nb[a], b);
        erase(nb[b], a);
        return true;
    };

    // Arterial points that did not become Gabriel neighbours: bridge them with
    // the shortest path of at most 4 street edges.
    {
        std::vector<uint32_t> parent(n, kNone), frontier, nextFrontier, visited;
        for (auto [a, b] : arterialPairs) {
            if (hasEdge(a, b)) continue;
            frontier = {a};
            parent[a] = a;
            visited   = {a};
            for (int depth = 0; depth < 4 && parent[b] == kNone; ++depth) {
                nextFrontier.clear();
                for (uint32_t u : frontier)
                    for (uint32_t v : nb[u])
                        if (parent[v] == kNone) {
                            parent[v] = u;
                            visited.push_back(v);
                            nextFrontier.push_back(v);
                        }
                frontier.swap(nextFrontier);
            }
            if (parent[b] != kNone)
                for (uint32_t u = b; u != a; u = parent[u]) arterial.insert(pairKey(u, parent[u]));
            for (uint32_t v : visited) parent[v] = kNone;
        }
    }

    // Triangles into quads: drop the longest edge of each triangle, keeping degree >= 2.
    {
        std::vector<std::pair<float, std::pair<uint32_t, uint32_t>>> cand;
        for (uint32_t u = 0; u < n; ++u)
            for (uint32_t v : nb[u]) {
                if (v < u) continue;
                const float luv = length2(pts[u] - pts[v]);
                for (uint32_t w : nb[u])
                    if (w != v && hasEdge(v, w) && luv > length2(pts[u] - pts[w]) && luv > length2(pts[v] - pts[w])) {
                        cand.push_back({luv, {u, v}});
                        break;
                    }
            }
        std::sort(cand.begin(), cand.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        for (const auto& [len, e] : cand) {
            const auto [u, v] = e;
            if (nb[u].size() >= 3 && nb[v].size() >= 3 && rng.uni() < 0.9f) removeEdge(u, v);
        }
    }

    // The longer of two streets meeting at a sharp angle goes.
    std::vector<std::pair<float, uint32_t>> sorted;
    for (uint32_t u = 0; u < n; ++u) {
        if (nb[u].size() < 3) continue;
        sorted.clear();
        for (uint32_t v : nb[u]) {
            const Vec2 d = pts[v] - pts[u];
            sorted.emplace_back(std::atan2(d.y, d.x), v);
        }
        std::sort(sorted.begin(), sorted.end());
        for (size_t k = 0; k < sorted.size(); ++k) {
            const auto [a0, va] = sorted[k];
            const auto [a1, vb] = sorted[(k + 1) % sorted.size()];
            float gap           = a1 - a0;
            if (gap < 0.0f) gap += 2.0f * kPi;
            if (gap >= 0.5f) continue; // ~28 degrees
            const uint32_t x = length2(pts[va] - pts[u]) > length2(pts[vb] - pts[u]) ? va : vb;
            if (nb[x].size() > 2 && removeEdge(u, x)) break;
        }
    }

    std::vector<std::pair<uint32_t, uint32_t>> es;
    for (uint32_t u = 0; u < n; ++u)
        for (uint32_t v : nb[u])
            if (u < v) es.emplace_back(u, v);
    std::shuffle(es.begin(), es.end(), rng.g);
    for (auto [a, b] : es)
        if (nb[a].size() >= 4 && nb[b].size() >= 4 && rng.uni() < 0.2f) removeEdge(a, b);

    std::vector<uint32_t> comp(n, kNone), stack;
    uint32_t              best = 0, bestSize = 0, nc = 0;
    for (uint32_t s = 0; s < n; ++s) {
        if (comp[s] != kNone) continue;
        uint32_t cnt = 0;
        comp[s]      = nc;
        stack.push_back(s);
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            ++cnt;
            for (uint32_t v : nb[u])
                if (comp[v] == kNone) {
                    comp[v] = nc;
                    stack.push_back(v);
                }
        }
        if (cnt > bestSize) {
            bestSize = cnt;
            best     = nc;
        }
        ++nc;
    }

    std::vector<uint32_t> remap(n, kNone);
    for (uint32_t u = 0; u < n; ++u)
        if (comp[u] == best) {
            remap[u] = uint32_t(m.nodes.size());
            m.nodes.push_back(pts[u]);
        }
    for (uint32_t u = 0; u < n; ++u) {
        if (remap[u] == kNone) continue;
        for (uint32_t v : nb[u])
            if (u < v)
                m.edges.push_back({remap[u], remap[v],
                                   arterial.count(pairKey(u, v)) ? RoadType::Arterial : RoadType::Local});
    }
    buildCsr(m);
}


} // namespace citygen
