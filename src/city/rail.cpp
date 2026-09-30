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


namespace {

Vec2 catmull(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
            (p3 - p0 + p1 * 3.0f - p2 * 3.0f) * t3) *
           0.5f;
}

struct RailBuilder {
    CityMap& m;

    // Stations closer than this are merged into one interchange.
    uint32_t station(Vec2 p, uint32_t line) {
        for (uint32_t i = 0; i < m.stations.size(); ++i) {
            if (length2(m.stations[i].pos - p) < 350.0f * 350.0f) {
                auto& l = m.stations[i].lines;
                if (std::find(l.begin(), l.end(), line) == l.end()) l.push_back(line);
                return i;
            }
        }
        m.stations.push_back({p, kNone, {line}, {}});
        return uint32_t(m.stations.size() - 1);
    }

    void addStation(RailLine& l, uint32_t lineIdx, Vec2 p) {
        const uint32_t s = station(p, lineIdx);
        if (l.stations.empty() || l.stations.back() != s) l.stations.push_back(s);
    }
};

std::vector<Vec2> walkTrack(const Field& f, Vec2 start, Vec2 dir, Rng& rng) {
    std::vector<Vec2> path{start};
    Vec2              p = start, h = dir;
    for (int i = 0; i < 400; ++i) {
        h            = normalize(rotate(h, rng.normal(0.07f)) * 0.85f + dir * 0.15f);
        const Vec2 q = p + h * 100.0f;
        if (!inside(q, f.size) || f.water(q)) break;
        path.push_back(q);
        p = q;
    }
    return path;
}

void placeStations(RailBuilder& rb, RailLine& line, uint32_t idx, const std::vector<Vec2>& anchors,
                   float gap, Rng& rng) {
    float acc = 0.0f, next = gap * rng.uni(0.8f, 1.2f);
    for (size_t i = 0; i < line.path.size(); ++i) {
        const Vec2 p = line.path[i];
        if (i > 0) acc += length(p - line.path[i - 1]);
        bool atAnchor = false;
        for (Vec2 a : anchors) atAnchor |= length2(a - p) < 80.0f * 80.0f;
        if (i == 0 || atAnchor || acc >= next) {
            rb.addStation(line, idx, p);
            acc  = 0.0f;
            next = gap * rng.uni(0.8f, 1.2f);
        }
    }
}

} // namespace

std::vector<Vec2> placeHubs(const Field& f, Rng& rng, Vec2 C, float R, int count) {
    std::vector<Vec2> hubs;
    const float       rot = rng.uni(0.0f, 2.0f * kPi);
    for (int i = 0; i < count; ++i) {
        const float a = rot + 2.0f * kPi * float(i) / float(count) + rng.uni(-0.25f, 0.25f);
        Vec2        h = C + Vec2{std::cos(a), std::sin(a)} * (R * rng.uni(0.85f, 1.15f));
        for (int k = 0; k < 20 && f.water(h); ++k) h = lerp(h, C, 0.15f);
        hubs.push_back(h);
    }
    return hubs;
}

// Metro: a ring line through the hubs, a cross-town line between two opposite hubs
// and radial lines out of the other hubs. Commuter rail: the cross-town line and a
// few radials, with longer headways and stations further apart.
void buildRail(const Field& f, Rng& rng, CityMap& m, Vec2 C, float R, const std::vector<Vec2>& hubs) {
    const CityPlan& plan = m.plan;
    const int       H    = int(hubs.size());
    if (plan.transit == Transit::None || plan.lines <= 0 || H < 2) return;

    const bool  metro    = plan.transit == Transit::Metro;
    const float crossGap = metro ? 1200.0f : 1500.0f;
    const float lineGap  = metro ? 1300.0f : 1500.0f;
    RailBuilder rb{m};

    auto ringPoint = [&](int seg, float t) {
        return catmull(hubs[size_t((seg + H - 1) % H)], hubs[size_t(seg)], hubs[size_t((seg + 1) % H)],
                       hubs[size_t((seg + 2) % H)], t);
    };

    if (metro) {
        RailLine ring;
        ring.loop    = true;
        ring.headway = 180.0f;
        for (int i = 0; i < H; ++i) {
            float len  = 0.0f;
            Vec2  prev = ringPoint(i, 0.0f);
            for (int k = 1; k <= 32; ++k) {
                const Vec2 q = ringPoint(i, float(k) / 32.0f);
                len += length(q - prev);
                prev = q;
            }
            const int samples = std::max(4, int(len / 40.0f));
            for (int k = 0; k < samples; ++k) ring.path.push_back(ringPoint(i, float(k) / float(samples)));
            const int ns = std::max(1, int(std::lround(len / 1100.0f)));
            for (int k = 0; k < ns; ++k) rb.addStation(ring, 0, ringPoint(i, float(k) / float(ns)));
        }
        m.lines.push_back(std::move(ring));
    }

    const int a = rng.below(H), b = (a + H / 2) % H;
    {
        const uint32_t    idx = uint32_t(m.lines.size());
        RailLine          line;
        line.crossTown        = true;
        line.headway          = metro ? 180.0f : 360.0f;
        const Vec2        ha = hubs[size_t(a)], hb = hubs[size_t(b)];
        std::vector<Vec2> outA = walkTrack(f, ha, rotate(normalize(ha - C), rng.uni(-0.3f, 0.3f)), rng);
        std::reverse(outA.begin(), outA.end());
        line.path = outA;

        const float side    = rng.uni() < 0.5f ? -1.0f : 1.0f;
        const Vec2  ctrl    = C + perp(normalize(hb - ha)) * (R * 0.5f * side);
        const int   samples = std::max(8, int(length(hb - ha) / 40.0f));
        for (int k = 1; k <= samples; ++k) {
            const float t = float(k) / float(samples);
            line.path.push_back(ha * ((1 - t) * (1 - t)) + ctrl * (2 * (1 - t) * t) + hb * (t * t));
        }
        const std::vector<Vec2> outB = walkTrack(f, hb, rotate(normalize(hb - C), rng.uni(-0.3f, 0.3f)), rng);
        line.path.insert(line.path.end(), outB.begin() + 1, outB.end());
        placeStations(rb, line, idx, {ha, hb}, crossGap, rng);
        m.lines.push_back(std::move(line));
    }

    const size_t      radials = size_t(std::max(0, plan.lines - (metro ? 2 : 1)));
    std::vector<Vec2> starts;
    for (int i = 0; i < H; ++i)
        if (i != a && i != b) starts.push_back(hubs[size_t(i)]);
    if (metro)
        while (starts.size() < radials) starts.push_back(ringPoint(rng.below(H), 0.5f));
    if (starts.size() > radials) starts.resize(radials);

    // Commuter radials branch off the cross-town line at its station nearest the hub,
    // so every line has an interchange.
    const std::vector<uint32_t> crossStations = m.lines.back().stations; // copy: m.lines grows below
    auto                        branchPoint   = [&](Vec2 hub) {
        Vec2  best  = hub;
        float bestD = kInf;
        for (uint32_t st : crossStations)
            if (length2(m.stations[st].pos - hub) < bestD) {
                bestD = length2(m.stations[st].pos - hub);
                best  = m.stations[st].pos;
            }
        return best;
    };

    for (const Vec2 s : starts) {
        RailLine line;
        line.headway          = metro ? 240.0f : 360.0f;
        std::vector<Vec2> out = walkTrack(f, s, rotate(normalize(s - C), rng.uni(-0.35f, 0.35f)), rng);
        if (out.size() < 15) continue;
        std::vector<Vec2> anchors{s};
        if (!metro) {
            const Vec2 from    = branchPoint(s);
            const int  samples = int(length(s - from) / 40.0f);
            for (int k = 0; k < samples; ++k) line.path.push_back(lerp(from, s, float(k) / float(samples)));
            anchors.push_back(from);
        }
        line.path.insert(line.path.end(), out.begin(), out.end());
        const uint32_t idx = uint32_t(m.lines.size());
        placeStations(rb, line, idx, anchors, lineGap, rng);
        m.lines.push_back(std::move(line));
    }
}


} // namespace citygen

std::vector<RailStop> railStops(const CityMap& m) {
    std::vector<RailStop> out;
    for (uint32_t l = 0; l < m.lines.size(); ++l) {
        const RailLine& line = m.lines[l];
        const size_t    n    = line.path.size();
        if (n < 2) continue;
        std::vector<uint32_t> seen;
        for (uint32_t s : line.stations) {
            if (std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
            seen.push_back(s);
            const Vec2 p = m.stations[s].pos;
            RailStop   best{p, {1.0f, 0.0f}, l, s};
            float      bestD = std::numeric_limits<float>::infinity();
            for (size_t i = 0; i + 1 < n + (line.loop ? 1 : 0); ++i) {
                const Vec2  a = line.path[i], b = line.path[(i + 1) % n], ab = b - a;
                const float l2 = length2(ab);
                if (l2 < 1e-6f) continue;
                const float t = std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f);
                const Vec2  q = a + ab * t;
                if (length2(q - p) < bestD) {
                    bestD    = length2(q - p);
                    best.pos = q;
                    best.dir = ab / std::sqrt(l2);
                }
            }
            out.push_back(best);
        }
    }
    return out;
}

