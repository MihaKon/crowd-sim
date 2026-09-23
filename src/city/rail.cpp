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

void buildRail(const Field& f, Rng& rng, CityMap& m, Vec2 C, float R, std::vector<Vec2>& hubs) {
    RailBuilder rb{m};
    constexpr int H = 6;

    const float rot = rng.uni(0.0f, 2.0f * kPi);
    for (int i = 0; i < H; ++i) {
        const float a = rot + 2.0f * kPi * float(i) / H + rng.uni(-0.25f, 0.25f);
        Vec2        h = C + Vec2{std::cos(a), std::sin(a)} * (R * rng.uni(0.85f, 1.15f));
        for (int k = 0; k < 20 && f.water(h); ++k) h = lerp(h, C, 0.15f);
        hubs.push_back(h);
    }

    auto ringPoint = [&](int seg, float t) {
        return catmull(hubs[size_t((seg + H - 1) % H)], hubs[size_t(seg)], hubs[size_t((seg + 1) % H)],
                       hubs[size_t((seg + 2) % H)], t);
    };

    {
        RailLine ring;
        ring.loop = true;
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
        placeStations(rb, line, idx, {ha, hb}, 1200.0f, rng);
        m.lines.push_back(std::move(line));
    }

    std::vector<Vec2> starts;
    for (int i = 0; i < H; ++i)
        if (i != a && i != b) starts.push_back(hubs[size_t(i)]);
    for (int j = 0; j < 2; ++j) starts.push_back(ringPoint(rng.below(H), 0.5f));

    for (const Vec2 s : starts) {
        RailLine line;
        line.path = walkTrack(f, s, rotate(normalize(s - C), rng.uni(-0.35f, 0.35f)), rng);
        if (line.path.size() < 15) continue;
        const uint32_t idx = uint32_t(m.lines.size());
        placeStations(rb, line, idx, {s}, 1300.0f, rng);
        m.lines.push_back(std::move(line));
    }
}


} // namespace citygen
