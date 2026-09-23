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


// Shrinks a polygon: each edge moves inward by `amount`, consecutive edges are re-intersected.
namespace {

std::vector<Vec2> insetPolygon(const std::vector<Vec2>& poly, float amount) {
    const size_t n = poly.size();
    if (n < 3) return {};
    float area2 = 0.0f;
    for (size_t i = 0; i < n; ++i) area2 += cross(poly[i], poly[(i + 1) % n]);
    const float sign = area2 > 0.0f ? 1.0f : -1.0f;

    std::vector<Vec2> out(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec2 prev = poly[(i + n - 1) % n], cur = poly[i], next = poly[(i + 1) % n];
        const Vec2 d1 = normalize(cur - prev), d2 = normalize(next - cur);
        const Vec2 n1 = perp(d1) * sign, n2 = perp(d2) * sign;
        const Vec2 a0 = prev + n1 * amount, a1 = cur + n1 * amount;
        const Vec2 b0 = cur + n2 * amount, b1 = next + n2 * amount;

        const Vec2  fallback = cur + (n1 + n2) * (0.5f * amount);
        const Vec2  r = a1 - a0, s = b1 - b0;
        const float denom = cross(r, s);
        Vec2        p = std::abs(denom) < 1e-6f ? fallback : a0 + r * (cross(b0 - a0, s) / denom);
        // Reflex corners can push the intersection far away (a polygon-offset spike):
        // fall back to the averaged normal.
        if (length2(p - cur) > 9.0f * amount * amount) p = fallback;
        out[i] = p;
    }
    return out;
}

float polygonArea(const std::vector<Vec2>& poly) { return std::abs(shoelace(poly)); }

struct RingParams {
    int   tilesAlongMin, tilesAlongMax, tilesInMin, tilesInMax;
    float gapChance;
};

struct Style {
    uint8_t roof, floors, facade, windows;
};

Style pickStyle(BlockType type, DistrictKind district, Rng& rng) {
    const float r = rng.uni(), r2 = rng.uni();
    auto        floors = [&](int lo, int hi) { return uint8_t(lo + rng.below(hi - lo + 1)); };
    switch (type) {
    case BlockType::Office:
        if (district == DistrictKind::Business)
            return {uint8_t(r < 0.5f ? 2 : 3), floors(8, 30), uint8_t(r2 < 0.7f ? 3 : 2), 1};
        return {uint8_t(r < 0.6f ? 2 : 3), floors(4, 8), 2, 1};
    case BlockType::Commercial: {
        const uint8_t roof = r < 0.4f ? 4 : r < 0.7f ? 3 : r < 0.85f ? 1 : 2;
        if (district == DistrictKind::Business) return {roof, floors(4, 12), uint8_t(r2 < 0.5f ? 3 : 1), 2};
        return {roof, floors(2, 6), uint8_t(district == DistrictKind::Poor ? 4 : 1), 2};
    }
    case BlockType::Industrial:
        return {uint8_t(r < 0.5f ? 3 : r < 0.8f ? 2 : 1), floors(2, 3), 5, 3};
    case BlockType::ResidentialRich:
        return {uint8_t(r < 0.5f ? 0 : r < 0.8f ? 5 : 1), floors(2, 3), 6, 0};
    case BlockType::ResidentialPoor:
        return {uint8_t(r < 0.5f ? 2 : r < 0.85f ? 1 : 3), floors(1, 3), uint8_t(r2 < 0.75f ? 4 : 5), 0};
    default:
        return {uint8_t(r < 0.45f ? 0 : r < 0.8f ? 1 : r < 0.95f ? 2 : 5), floors(2, 5), uint8_t(r2 < 0.5f ? 0 : 1), 0};
    }
}

RingParams ringParamsFor(BlockType type, DistrictKind district) {
    switch (type) {
    case BlockType::Commercial:      return {4, 7, 3, 5, 0.04f};
    case BlockType::Office:          return district == DistrictKind::Business ? RingParams{5, 9, 5, 8, 0.03f}
                                                                               : RingParams{5, 8, 4, 6, 0.03f};
    case BlockType::Industrial:      return {6, 10, 5, 8, 0.08f};
    case BlockType::ResidentialRich: return {4, 5, 3, 4, 0.30f};
    case BlockType::ResidentialPoor: return {3, 4, 3, 3, 0.02f};
    default:                         return {4, 6, 3, 5, 0.07f};
    }
}

struct Obb {
    Vec2  c, ax, ay;
    float hx, hy;
};

Obb obbOf(Vec2 origin, Vec2 along, Vec2 in, float sa, float si) {
    return {origin + along * (0.5f * sa) + in * (0.5f * si), along, in, 0.5f * sa, 0.5f * si};
}

// Separating axis test; rectangles that only touch (shared walls) do not count.
bool overlaps(const Obb& a, const Obb& b) {
    const Vec2 d = b.c - a.c;
    for (Vec2 L : {a.ax, a.ay, b.ax, b.ay}) {
        const float ra = a.hx * std::abs(dot(a.ax, L)) + a.hy * std::abs(dot(a.ay, L));
        const float rb = b.hx * std::abs(dot(b.ax, L)) + b.hy * std::abs(dot(b.ay, L));
        if (std::abs(dot(d, L)) >= ra + rb - 0.05f) return false;
    }
    return true;
}

bool insidePolygon(const std::vector<Vec2>& poly, Vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
            p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            in = !in;
    return in;
}

float segmentDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2  ab = b - a;
    const float l2 = length2(ab);
    const float t  = l2 > 0.0f ? std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
    return length(p - (a + ab * t));
}

// Street centre lines by grid cell: the block polygon only approximates the kerb,
// so lots are also checked against the real streets.
struct StreetGrid {
    float                              cell = 100.0f;
    int                                n    = 1;
    std::vector<std::vector<uint32_t>> edges;
    const CityMap*                     m = nullptr;

    void build(const CityMap& city) {
        m = &city;
        n = int(city.size / cell) + 1;
        edges.assign(size_t(n) * n, {});
        for (uint32_t e = 0; e < city.edges.size(); ++e) {
            const Vec2 a = city.nodes[city.edges[e].a], b = city.nodes[city.edges[e].b];
            const int  x0 = std::clamp(int(std::min(a.x, b.x) / cell), 0, n - 1), x1 = std::clamp(int(std::max(a.x, b.x) / cell), 0, n - 1);
            const int  y0 = std::clamp(int(std::min(a.y, b.y) / cell), 0, n - 1), y1 = std::clamp(int(std::max(a.y, b.y) / cell), 0, n - 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) edges[size_t(y) * n + size_t(x)].push_back(e);
        }
    }

    bool clear(const Vec2* pts, int count, float sidewalk) const {
        for (int k = 0; k < count; ++k) {
            const int x = std::clamp(int(pts[k].x / cell), 0, n - 1), y = std::clamp(int(pts[k].y / cell), 0, n - 1);
            for (int yy = std::max(0, y - 1); yy <= std::min(n - 1, y + 1); ++yy)
                for (int xx = std::max(0, x - 1); xx <= std::min(n - 1, x + 1); ++xx)
                    for (uint32_t e : edges[size_t(yy) * n + size_t(xx)]) {
                        const Edge& ed = m->edges[e];
                        if (segmentDistance(pts[k], m->nodes[ed.a], m->nodes[ed.b]) < 0.5f * roadWidth(ed.type) + sidewalk)
                            return false;
                    }
        }
        return true;
    }
};

bool fits(const Obb& o, const std::vector<Vec2>& block, float margin, const std::vector<Obb>& placed,
          const StreetGrid& streets) {
    for (const Obb& other : placed)
        if (overlaps(o, other)) return false;
    const Vec2 corners[4] = {o.c - o.ax * o.hx - o.ay * o.hy, o.c + o.ax * o.hx - o.ay * o.hy,
                             o.c + o.ax * o.hx + o.ay * o.hy, o.c - o.ax * o.hx + o.ay * o.hy};
    for (const Vec2& p : corners) {
        if (!insidePolygon(block, p)) return false;
        for (size_t i = 0; i < block.size(); ++i)
            if (segmentDistance(p, block[i], block[(i + 1) % block.size()]) < margin) return false;
    }
    for (const Vec2& v : block) { // a reflex block corner inside the lot
        const Vec2 d = v - o.c;
        if (std::abs(dot(d, o.ax)) < o.hx && std::abs(dot(d, o.ay)) < o.hy) return false;
    }
    const Vec2 probes[8] = {corners[0], corners[1], corners[2], corners[3],
                            (corners[0] + corners[1]) * 0.5f, (corners[1] + corners[2]) * 0.5f,
                            (corners[2] + corners[3]) * 0.5f, (corners[3] + corners[0]) * 0.5f};
    return streets.clear(probes, 8, 2.0f);
}

// Lots edge to edge along the polygon; one that does not fit is tried shallower and
// set back before it becomes a gap. Returns the deepest lot (inset for the next ring).
float placeRing(const std::vector<Vec2>& poly, const std::vector<Vec2>& block, float margin, BlockType type,
                DistrictKind district, Rng& rng, const StreetGrid& streets, std::vector<Obb>& placed,
                std::vector<Building>& out) {
    const RingParams p = ringParamsFor(type, district);
    const float      T = kBuildingTileMetres;
    const float      wMin = float(p.tilesAlongMin) * T;

    float area2 = 0.0f;
    for (size_t i = 0; i < poly.size(); ++i) area2 += cross(poly[i], poly[(i + 1) % poly.size()]);
    const float sign = area2 > 0.0f ? 1.0f : -1.0f;

    float maxDepth = 0.0f;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2  a = poly[i], b = poly[(i + 1) % poly.size()];
        const Vec2  full = b - a;
        const float len = length(full);
        if (len < wMin) continue;
        const Vec2 dir = full / len, inward = perp(dir) * sign;

        float t = 0.0f;
        while (t < len - 1e-3f) {
            int   tilesAlong = p.tilesAlongMin + rng.below(p.tilesAlongMax - p.tilesAlongMin + 1);
            float w = float(tilesAlong) * T;
            if (t + w > len) {
                const float remain = len - t;
                if (remain < wMin) break;
                tilesAlong = std::max(p.tilesAlongMin, int(remain / T));
                w          = float(tilesAlong) * T;
                if (t + w > len + 0.05f) break;
            }
            const Style style = pickStyle(type, district, rng);
            if (rng.uni() >= p.gapChance) {
                bool done = false;
                for (int tilesIn = p.tilesInMin + rng.below(p.tilesInMax - p.tilesInMin + 1);
                     !done && tilesIn >= p.tilesInMin; --tilesIn)
                    for (float setback : {0.0f, 1.0f, 2.0f}) {
                        const Vec2 origin = a + dir * t + inward * setback;
                        const Obb  o      = obbOf(origin, dir, inward, w, float(tilesIn) * T);
                        if (!fits(o, block, margin, placed, streets)) continue;
                        placed.push_back(o);
                        out.push_back({origin, dir, inward, uint8_t(tilesAlong), uint8_t(tilesIn), style.roof,
                                       style.floors, style.facade, style.windows});
                        maxDepth = std::max(maxDepth, float(tilesIn) * T + setback);
                        done     = true;
                        break;
                    }
            }
            t += w;
        }
    }
    return maxDepth;
}

} // namespace

void buildBuildings(CityMap& m, Rng& rng) {
    constexpr float kSetback = 2.8f; // m: block.poly stops at the carriageway, add the sidewalk
    constexpr float kMinArea = 40.0f;
    constexpr float kRingGap = 1.0f;


    StreetGrid streets;
    streets.build(m);
    for (const Block& blk : m.blocks) {
        if (blk.type == BlockType::Park) continue;
        const std::vector<Vec2> lot1 = insetPolygon(blk.poly, kSetback);
        if (lot1.size() < 3 || polygonArea(lot1) < kMinArea || polygonArea(lot1) >= polygonArea(blk.poly)) continue;

        const DistrictKind district = m.districts[blk.district].kind;
        const int          rings    = blk.type == BlockType::ResidentialRich   ? 1
                                    : blk.type == BlockType::ResidentialPoor ? 4
                                                                             : 2;
        std::vector<Obb>   placed;
        std::vector<Vec2>  lot = lot1;
        for (int ring = 0; ring < rings; ++ring) {
            const float depth =
                placeRing(lot, blk.poly, kSetback - 0.1f, blk.type, district, rng, streets, placed, m.buildings);
            if (depth <= 0.0f || ring + 1 == rings) break;
            std::vector<Vec2> next = insetPolygon(lot, depth + kRingGap);
            if (next.size() < 3 || polygonArea(next) < kMinArea || polygonArea(next) >= polygonArea(lot)) break;
            lot = std::move(next);
        }
    }
}


} // namespace citygen
