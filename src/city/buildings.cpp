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

// height: CityPlan::towerFloors / 30. Towers scale with it, mid-rises with its
// square root, low-rise housing and industry stay as they are.
Style pickStyle(BlockType type, DistrictKind district, float height, Rng& rng) {
    const float r = rng.uni(), r2 = rng.uni();
    auto        floors = [&](int lo, int hi) { return uint8_t(lo + rng.below(hi - lo + 1)); };
    auto        tower  = [&](int lo, int hi) {
        const int l = std::clamp(int(std::lround(float(lo) * height)), 1, 250);
        return floors(l, std::clamp(int(std::lround(float(hi) * height)), l, 250));
    };
    auto        mid    = [&](int lo, int hi) {
        return floors(lo, std::max(lo, int(std::lround(float(hi) * std::sqrt(height)))));
    };
    switch (type) {
    case BlockType::Office:
        if (district == DistrictKind::Business)
            return {uint8_t(r < 0.5f ? 2 : 3), tower(8, 30), uint8_t(r2 < 0.7f ? 3 : 2), 1};
        return {uint8_t(r < 0.6f ? 2 : 3), mid(4, 8), 2, 1};
    case BlockType::Commercial: {
        const uint8_t roof = r < 0.4f ? 4 : r < 0.7f ? 3 : r < 0.85f ? 1 : 2;
        if (district == DistrictKind::Business) return {roof, tower(4, 12), uint8_t(r2 < 0.5f ? 3 : 1), 2};
        return {roof, mid(2, 6), uint8_t(district == DistrictKind::Poor ? 4 : 1), 2};
    }
    case BlockType::Industrial:
        return {uint8_t(r < 0.5f ? 3 : r < 0.8f ? 2 : 1), floors(2, 3), 5, 3};
    case BlockType::ResidentialRich:
        return {uint8_t(r < 0.5f ? 0 : r < 0.8f ? 5 : 1), floors(2, 3), 6, 0};
    case BlockType::ResidentialPoor:
        return {uint8_t(r < 0.5f ? 2 : r < 0.85f ? 1 : 3), floors(1, 3), uint8_t(r2 < 0.75f ? 4 : 5), 0};
    default:
        return {uint8_t(r < 0.45f ? 0 : r < 0.8f ? 1 : r < 0.95f ? 2 : 5), mid(2, 5), uint8_t(r2 < 0.5f ? 0 : 1), 0};
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

    // Rail corridors, platforms and station squares: no building may overlap them.
    std::vector<Obb>                   keepOut;
    std::vector<std::vector<uint32_t>> keepCells;

    void addKeepOut(const Obb& o) {
        if (keepCells.empty()) keepCells.assign(size_t(n) * n, {});
        const float ex = std::abs(o.ax.x) * o.hx + std::abs(o.ay.x) * o.hy; // half extents of its bounding box
        const float ey = std::abs(o.ax.y) * o.hx + std::abs(o.ay.y) * o.hy;
        auto cellOf = [&](float v) { return std::clamp(int(v / cell), 0, n - 1); };
        const int x0 = cellOf(o.c.x - ex), x1 = cellOf(o.c.x + ex), y0 = cellOf(o.c.y - ey), y1 = cellOf(o.c.y + ey);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) keepCells[size_t(y) * n + size_t(x)].push_back(uint32_t(keepOut.size()));
        keepOut.push_back(o);
    }

    void buildRail(const CityMap& city) {
        for (const RailLine& l : city.lines) {
            const size_t k = l.path.size();
            for (size_t i = 0; i + 1 < k + (l.loop && k > 1 ? 1 : 0); ++i) {
                const Vec2  a = l.path[i], b = l.path[(i + 1) % k];
                const float len = length(b - a);
                if (len < 1e-3f) continue;
                const Vec2 d = (b - a) / len;
                addKeepOut({(a + b) * 0.5f, d, perp(d), 0.5f * len + 2.0f, kRailCorridor});
            }
        }
        for (const RailStop& s : railStops(city))
            addKeepOut({s.pos, s.dir, perp(s.dir), 0.5f * kPlatformLength + 4.0f, kPlatformHalf + 1.5f});
        for (const Station& s : city.stations)
            addKeepOut({s.pos, {1.0f, 0.0f}, {0.0f, 1.0f}, 0.5f * kPlazaSize + 1.0f, 0.5f * kPlazaSize + 1.0f});
    }

    bool clearOfRail(const Obb& o) const {
        if (keepCells.empty()) return true;
        const int x = std::clamp(int(o.c.x / cell), 0, n - 1), y = std::clamp(int(o.c.y / cell), 0, n - 1);
        for (int yy = std::max(0, y - 1); yy <= std::min(n - 1, y + 1); ++yy)
            for (int xx = std::max(0, x - 1); xx <= std::min(n - 1, x + 1); ++xx)
                for (uint32_t i : keepCells[size_t(yy) * n + size_t(xx)])
                    if (overlaps(o, keepOut[i])) return false;
        return true;
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
    return streets.clear(probes, 8, 2.0f) && streets.clearOfRail(o);
}

// Lots edge to edge along the polygon; one that does not fit is tried shallower and
// set back before it becomes a gap. Returns the deepest lot (inset for the next ring).
float placeRing(const std::vector<Vec2>& poly, const std::vector<Vec2>& block, float margin, BlockType type,
                DistrictKind district, float height, Rng& rng, const StreetGrid& streets, std::vector<Obb>& placed,
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
            const Style style = pickStyle(type, district, height, rng);
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

// ---- trees: their own random stream, so buildings do not depend on them.

bool insideObb(const Obb& o, Vec2 p, float grow) {
    const Vec2 d = p - o.c;
    return std::abs(dot(d, o.ax)) < o.hx + grow && std::abs(dot(d, o.ay)) < o.hy + grow;
}

float edgeDistance(const std::vector<Vec2>& poly, Vec2 p) {
    float d = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < poly.size(); ++i) d = std::min(d, segmentDistance(p, poly[i], poly[(i + 1) % poly.size()]));
    return d;
}

// Gardens and yards: free ground inside a block, away from the kerb and the buildings.
void plantBlock(const Block& blk, const std::vector<Obb>& placed, const StreetGrid& streets, Vec2 palace,
                float palaceRadius, Rng& rng, std::vector<Tree>& out) {
    float perTree; // m² of block per tree
    float rMin = 2.2f, rMax = 3.6f;
    switch (blk.type) {
    case BlockType::ResidentialRich: perTree = 90.0f; rMin = 2.8f; rMax = 4.8f; break;
    case BlockType::Residential:     perTree = 320.0f; break;
    case BlockType::ResidentialPoor: perTree = 800.0f; break;
    case BlockType::Office:          perTree = 520.0f; break;
    case BlockType::Commercial:      perTree = 1100.0f; break;
    case BlockType::Industrial:      perTree = 2500.0f; break;
    case BlockType::Park:            perTree = 55.0f; rMin = 2.5f; rMax = 5.0f; break;
    default:                         perTree = 1000.0f; break;
    }
    Vec2 lo = blk.poly[0], hi = blk.poly[0];
    for (Vec2 p : blk.poly) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    const bool palaceGrounds = blk.type == BlockType::Park && length(blk.centroid - palace) < palaceRadius;
    const int  tries         = int(blk.area / perTree * 1.6f) + 1;
    for (int t = 0; t < tries; ++t) {
        const Vec2  p{rng.uni(lo.x, hi.x), rng.uni(lo.y, hi.y)};
        const float r = rng.uni(rMin, rMax);
        if (!insidePolygon(blk.poly, p) || edgeDistance(blk.poly, p) < 3.0f + 0.5f * r) continue;
        if (blk.type == BlockType::Park) {
            // Groves and open lawns; a clearing around the palace itself.
            const float grove = fbm(p * (1.0f / 90.0f), 977u);
            if (grove < 0.42f + 0.25f * rng.uni()) continue;
            if (palaceGrounds && length(p - palace) < 0.3f * palaceRadius) continue; // clearing and moat
        }
        bool free = true;
        for (const Obb& o : placed)
            if (insideObb(o, p, 0.6f * r)) {
                free = false;
                break;
            }
        if (!free || !streets.clearOfRail({p, {1, 0}, {0, 1}, r, r})) continue;
        const TreeKind kind = rng.uni() < (blk.type == BlockType::Park ? 0.25f : 0.12f) ? TreeKind::Conifer
                                                                                        : TreeKind::Broadleaf;
        out.push_back({p, kind == TreeKind::Conifer ? r * 0.75f : r, kind});
    }
}

// Rows of street trees along the arterials (both sides) and some local streets.
void plantStreets(const CityMap& m, const StreetGrid& streets, Rng& rng, std::vector<Tree>& out) {
    for (const Edge& e : m.edges) {
        const bool  art = e.type == RoadType::Arterial;
        if (!art && rng.uni() > 0.35f) continue;
        const Vec2  a = m.nodes[e.a], b = m.nodes[e.b];
        const float len = length(b - a);
        if (len < 40.0f) continue;
        const Vec2  d = (b - a) / len, n = perp(d);
        const float gap = art ? 12.0f : 17.0f, offset = 0.5f * roadWidth(e.type) + 1.25f;
        const float sides = art ? 2.0f : 1.0f;
        for (float s = 0; s < sides; s += 1.0f) {
            const float side = (s == 0.0f) == (rng.uni() < 0.5f) ? 1.0f : -1.0f;
            for (float t = 15.0f + rng.uni(0.0f, gap); t < len - 15.0f; t += gap) {
                const Vec2 p = a + d * t + n * (offset * side);
                if (!streets.clearOfRail({p, {1, 0}, {0, 1}, 2.0f, 2.0f})) continue;
                out.push_back({p, art ? rng.uni(2.6f, 3.4f) : rng.uni(2.0f, 2.8f), TreeKind::Street});
            }
        }
    }
}

// The palace in the clearing at the centre of its park: a main hall and two wings
// around a courtyard, white walls under dark tiled roofs.
void addPalace(CityMap& m, Rng& rng) {
    const float a  = rng.uni(0.0f, 0.5f);
    const Vec2  ax = {std::cos(a), std::sin(a)}, ay = perp(ax);
    auto hall = [&](Vec2 centre, int tilesAlong, int tilesIn, uint8_t floors) {
        const Vec2 origin = centre - ax * (0.5f * float(tilesAlong) * kBuildingTileMetres) -
                            ay * (0.5f * float(tilesIn) * kBuildingTileMetres);
        m.buildings.push_back({origin, ax, ay, uint8_t(tilesAlong), uint8_t(tilesIn), 0, floors, 6, 0});
    };
    hall(m.palace + ay * 12.0f, 18, 7, 3);
    hall(m.palace - ax * 48.0f - ay * 16.0f, 5, 12, 2);
    hall(m.palace + ax * 48.0f - ay * 16.0f, 5, 12, 2);
}

} // namespace

void buildBuildings(CityMap& m, Rng& rng) {
    constexpr float kSetback = 2.8f; // m: block.poly stops at the carriageway, add the sidewalk
    constexpr float kMinArea = 40.0f;
    constexpr float kRingGap = 1.0f;


    const float height = m.plan.towerFloors / 30.0f;
    StreetGrid  streets;
    streets.build(m);
    streets.buildRail(m);
    Rng treeRng(m.seed ^ 0x5eed7ee5u);
    for (Block& blk : m.blocks) {
        if (blk.type == BlockType::Park) {
            if (m.palaceRadius > 0.0f && insidePolygon(blk.poly, m.palace)) addPalace(m, treeRng);
            plantBlock(blk, {}, streets, m.palace, m.palaceRadius, treeRng, m.trees);
            continue;
        }
        const std::vector<Vec2> lot1 = insetPolygon(blk.poly, kSetback);
        if (lot1.size() < 3 || polygonArea(lot1) < kMinArea || polygonArea(lot1) >= polygonArea(blk.poly)) continue;

        const DistrictKind district = m.districts[blk.district].kind;
        const int          rings    = blk.type == BlockType::ResidentialRich   ? 1
                                    : blk.type == BlockType::ResidentialPoor ? 4
                                                                             : 2;
        std::vector<Obb>   placed;
        std::vector<Vec2>  lot   = lot1;
        const size_t       first = m.buildings.size();
        for (int ring = 0; ring < rings; ++ring) {
            const float depth = placeRing(lot, blk.poly, kSetback - 0.1f, blk.type, district, height, rng, streets,
                                          placed, m.buildings);
            if (depth <= 0.0f || ring + 1 == rings) break;
            std::vector<Vec2> next = insetPolygon(lot, depth + kRingGap);
            if (next.size() < 3 || polygonArea(next) < kMinArea || polygonArea(next) >= polygonArea(lot)) break;
            lot = std::move(next);
        }
        for (size_t i = first; i < m.buildings.size(); ++i) {
            const Building& b = m.buildings[i];
            blk.floorArea += float(b.tilesAlong) * float(b.tilesIn) * kBuildingTileMetres * kBuildingTileMetres *
                             float(b.floors);
        }
        plantBlock(blk, placed, streets, m.palace, m.palaceRadius, treeRng, m.trees);
    }
    plantStreets(m, streets, treeRng, m.trees);
}


} // namespace citygen
