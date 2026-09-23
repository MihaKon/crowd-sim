#pragma once
#include "city/mapgen.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>
#include <vector>

namespace citygen {

constexpr float    kPi         = 3.14159265f;
constexpr float    kMinSpacing = 55.0f;  // m, street node spacing: densest areas
constexpr float    kMaxSpacing = 190.0f; // m, outskirts
constexpr uint32_t kNone       = 0xFFFFFFFFu;
constexpr float    kInf        = std::numeric_limits<float>::infinity();


struct Rng {
    std::mt19937 g;
    explicit Rng(uint32_t seed) : g(seed) {}
    float uni(float a = 0.0f, float b = 1.0f) { return std::uniform_real_distribution<float>(a, b)(g); }
    float normal(float sigma) { return std::normal_distribution<float>(0.0f, sigma)(g); }
    int   below(int n) { return std::uniform_int_distribution<int>(0, n - 1)(g); }
};

inline uint32_t hashU(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float hashF(int x, int y, uint32_t seed) {
    return float(hashU(uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ seed)) *
           (1.0f / 4294967296.0f);
}

inline float valueNoise(Vec2 p, uint32_t seed) {
    const float fx = std::floor(p.x), fy = std::floor(p.y);
    const int   ix = int(fx), iy = int(fy);
    float       tx = p.x - fx, ty = p.y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = hashF(ix, iy, seed), b = hashF(ix + 1, iy, seed);
    const float c = hashF(ix, iy + 1, seed), d = hashF(ix + 1, iy + 1, seed);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}

inline float fbm(Vec2 p, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (uint32_t o = 0; o < 4; ++o) {
        sum += amp * valueNoise(p, seed + o * 101u);
        norm += amp;
        p = p * 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

inline bool inside(Vec2 p, float size) { return p.x >= 0.0f && p.y >= 0.0f && p.x <= size && p.y <= size; }


struct Bump {
    Vec2  c;
    float sigma, w;
};

struct Field {
    float             size = 0.0f;
    uint32_t          seed = 0;
    Vec2              bayCenter;
    float             bayRadius = 0.0f;
    Vec2              palace;
    float             palaceRadius = 0.0f;
    std::vector<Bump> bumps;

    bool water(Vec2 p) const {
        if (bayRadius <= 0.0f) return false;
        const float n = fbm(p * (1.0f / 2500.0f), seed + 11u) - 0.5f;
        return length(p - bayCenter) < bayRadius * (1.0f + 0.6f * n);
    }

    float density(Vec2 p) const {
        float d = 0.12f + 0.25f * fbm(p * (1.0f / 1800.0f), seed + 23u);
        for (const Bump& b : bumps) {
            const float r2 = length2(p - b.c);
            const float s2 = b.sigma * b.sigma;
            if (r2 < 9.0f * s2) d += b.w * std::exp(-r2 / (2.0f * s2));
        }
        if (length(p - palace) < palaceRadius) d = 0.0f;
        return std::clamp(d, 0.0f, 1.0f);
    }
};

struct ScalarGrid {
    int                n    = 0;
    float              cell = 1.0f;
    std::vector<float> v;

    template <class F> void build(float size, float cellSize, F&& fn) {
        cell = cellSize;
        n    = int(std::ceil(size / cell));
        v.resize(size_t(n + 1) * size_t(n + 1));
        for (int y = 0; y <= n; ++y)
            for (int x = 0; x <= n; ++x) v[size_t(y) * size_t(n + 1) + size_t(x)] = fn(Vec2{x * cell, y * cell});
    }

    float sample(Vec2 p) const {
        const float  fx = std::clamp(p.x / cell, 0.0f, float(n));
        const float  fy = std::clamp(p.y / cell, 0.0f, float(n));
        const int    x0 = std::min(int(fx), n - 1), y0 = std::min(int(fy), n - 1);
        const float  tx = fx - float(x0), ty = fy - float(y0);
        const size_t w = size_t(n) + 1, i = size_t(y0) * w + size_t(x0);
        return lerpf(lerpf(v[i], v[i + 1], tx), lerpf(v[i + w], v[i + w + 1], tx), ty);
    }
};

struct PointGrid {
    float                 cell = 1.0f;
    int                   n    = 1;
    std::vector<uint32_t> start, items;

    size_t cellIndex(Vec2 p) const {
        const int x = std::clamp(int(p.x / cell), 0, n - 1);
        const int y = std::clamp(int(p.y / cell), 0, n - 1);
        return size_t(y) * size_t(n) + size_t(x);
    }

    void build(const std::vector<Vec2>& pts, float size, float cellSize) {
        cell = cellSize;
        n    = std::max(1, int(std::ceil(size / cell)));
        start.assign(size_t(n) * size_t(n) + 1, 0);
        for (const Vec2& p : pts) ++start[cellIndex(p) + 1];
        for (size_t i = 1; i < start.size(); ++i) start[i] += start[i - 1];
        items.resize(pts.size());
        std::vector<uint32_t> fill(start.begin(), start.end() - 1);
        for (uint32_t i = 0; i < pts.size(); ++i) items[fill[cellIndex(pts[i])]++] = i;
    }

    template <class F> void query(Vec2 c, float r, F&& fn) const {
        const int x0 = std::clamp(int((c.x - r) / cell), 0, n - 1);
        const int x1 = std::clamp(int((c.x + r) / cell), 0, n - 1);
        const int y0 = std::clamp(int((c.y - r) / cell), 0, n - 1);
        const int y1 = std::clamp(int((c.y + r) / cell), 0, n - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const size_t ci = size_t(y) * size_t(n) + size_t(x);
                for (uint32_t k = start[ci]; k < start[ci + 1]; ++k) fn(items[k]);
            }
    }

    uint32_t nearest(const std::vector<Vec2>& pts, Vec2 p) const {
        uint32_t best  = kNone;
        float    bestD = kInf;
        for (float r = cell;; r *= 2.0f) {
            query(p, r, [&](uint32_t i) {
                const float d = length2(pts[i] - p);
                if (d < bestD) {
                    bestD = d;
                    best  = i;
                }
            });
            if ((best != kNone && bestD <= r * r) || r > 4.0f * cell * float(n)) return best;
        }
    }
};



inline float shoelace(const std::vector<Vec2>& p) {
    float a = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) a += cross(p[i], p[(i + 1) % p.size()]);
    return 0.5f * a;
}


struct Seeded {
    std::vector<Vec2>                          pts;
    std::vector<std::pair<uint32_t, uint32_t>> arterialPairs;
};

void buildRail(const Field& f, Rng& rng, CityMap& m, Vec2 C, float R, std::vector<Vec2>& hubs);
std::vector<std::vector<Vec2>> planArterials(const Field& f, Vec2 C, float R, Rng& rng);
Seeded poissonDisk(const Field& f, const ScalarGrid& spacing, const std::vector<std::vector<Vec2>>& arterials,
                   Rng& rng);
std::vector<std::pair<uint32_t, uint32_t>> gabriel(const std::vector<Vec2>& pts, const ScalarGrid& spacing,
                                                   const PointGrid& pg);
void buildStreetGraph(CityMap& m, const std::vector<Vec2>& pts,
                      const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                      const std::vector<std::pair<uint32_t, uint32_t>>& arterialPairs, Rng& rng);
void buildDistricts(CityMap& m, const Field& f, Vec2 C, float R, const std::vector<Vec2>& hubs, Rng& rng);
void extractBlocks(CityMap& m, const Field& f, Rng& rng);
void nameStations(CityMap& m, Rng& rng);
void buildBuildings(CityMap& m, Rng& rng);
void buildWater(CityMap& m, const Field& f);

} // namespace citygen
