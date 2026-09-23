#include "city/sim_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <queue>

namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr float    kInf  = std::numeric_limits<float>::infinity();

uint32_t bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

float projectArc(const std::vector<Vec2>& pts, const std::vector<float>& cum, Vec2 p) {
    float best = kInf, arc = 0.0f;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2  a = pts[i], ab = pts[i + 1] - a;
        const float l2 = length2(ab);
        const float t  = l2 > 0.0f ? std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
        const float d  = length2(p - (a + ab * t));
        if (d < best) {
            best = d;
            arc  = cum[i] + t * std::sqrt(l2);
        }
    }
    return arc;
}

struct Rail {
    std::vector<std::vector<Vec2>>  poly;
    std::vector<std::vector<float>> cum;
    std::vector<float>              arc;
    std::vector<uint32_t>           table;

    std::vector<uint32_t> lineDir, profile, profileIndex, trainSlots;
    uint32_t              occupancyCount = 0;
};

constexpr uint32_t kDayMs      = 24u * 3600u * 1000u;
constexpr uint32_t kFirstTrain = 5u * 3600u * 1000u;  // 05:00
constexpr uint32_t kLastTrain  = 23u * 3600u * 1000u; // 23:00, last radial departure

// Arrival / departure offsets at each station for a train leaving the first one at t = 0.
void buildTimetables(const CityMap& m, Rail& r) {
    const uint32_t S = uint32_t(m.stations.size()), L = uint32_t(m.lines.size());
    r.profileIndex.assign(size_t(L) * 2 * S, kNone);

    for (uint32_t l = 0; l < L; ++l) {
        const bool  loop  = m.lines[l].loop;
        const float total = r.cum[l].back();

        std::vector<std::pair<float, uint32_t>> st;
        for (uint32_t s : m.lines[l].stations)
            if (std::none_of(st.begin(), st.end(), [&](const auto& x) { return x.second == s; }))
                st.emplace_back(r.arc[size_t(l) * S + s], s);
        std::sort(st.begin(), st.end());

        for (uint32_t d = 0; d < 2; ++d) {
            const uint32_t ld = l * 2 + d;
            auto           seq = st;
            if (d == 1) std::reverse(seq.begin(), seq.end());
            if (loop && seq.size() >= 2) seq.emplace_back(seq.front().first + (d == 0 ? total : -total), seq.front().second);

            const uint32_t first = uint32_t(r.profile.size() / 4);
            uint32_t       arr = 0, dep = 0;
            for (size_t k = 0; k < seq.size(); ++k) {
                if (k > 0) {
                    arr = dep + uint32_t(std::abs(seq[k].first - seq[k - 1].first) / kTrainSpeed * 1000.0f);
                    dep = (!loop && k + 1 == seq.size()) ? arr : arr + uint32_t(kRailDwell * 1000.0f);
                }
                r.profile.insert(r.profile.end(), {seq[k].second, bits(seq[k].first), arr, dep});
                const size_t pi = size_t(ld) * S + seq[k].second;
                if (r.profileIndex[pi] == kNone && !(loop && k + 1 == seq.size())) r.profileIndex[pi] = uint32_t(k);
            }

            const uint32_t count   = uint32_t(seq.size());
            uint32_t       headway = uint32_t((loop || l == 1 ? kLoopHeadway : kLineHeadway) * 1000.0f);
            uint32_t       span = dep, fleet = 0, slots = 0;
            if (count < 2) {
                fleet = 0;
            } else if (loop) {
                // A fixed fleet circulates; the headway is adjusted so the fleet divides the period.
                fleet   = std::max(1u, uint32_t(std::lround(double(dep) / headway)));
                headway = dep / fleet;
                span    = headway * fleet;
                slots   = fleet;
            } else {
                fleet = (kLastTrain - kFirstTrain) / headway + 1; // trips per day
                slots = span / headway + 2;                       // trains on the line at once
            }
            r.lineDir.insert(r.lineDir.end(), {first, count, loop ? 1u : 0u, l, headway, span, fleet, r.occupancyCount});
            r.occupancyCount += fleet;
            for (uint32_t k = 0; k < slots; ++k) r.trainSlots.insert(r.trainSlots.end(), {ld, k});
        }
    }
}

Rail buildRail(const CityMap& m) {
    const uint32_t S = uint32_t(m.stations.size()), L = uint32_t(m.lines.size());
    Rail           r;
    r.poly.resize(L);
    r.cum.resize(L);
    r.arc.assign(size_t(L) * S, -1.0f);

    for (uint32_t l = 0; l < L; ++l) {
        const RailLine& line = m.lines[l];
        r.poly[l]            = line.path;
        if (line.loop) r.poly[l].push_back(line.path.front());
        r.cum[l].assign(r.poly[l].size(), 0.0f);
        for (size_t i = 1; i < r.poly[l].size(); ++i)
            r.cum[l][i] = r.cum[l][i - 1] + length(r.poly[l][i] - r.poly[l][i - 1]);
        for (uint32_t s : line.stations) r.arc[size_t(l) * S + s] = projectArc(r.poly[l], r.cum[l], m.stations[s].pos);
    }

    // Graph over (station, line) states: ride edges along lines, transfer edges within stations.
    std::vector<int32_t>  stateOf(size_t(L) * S, -1);
    std::vector<uint32_t> stStation, stLine;
    for (uint32_t l = 0; l < L; ++l)
        for (uint32_t s : m.lines[l].stations)
            if (stateOf[size_t(l) * S + s] < 0) {
                stateOf[size_t(l) * S + s] = int32_t(stStation.size());
                stStation.push_back(s);
                stLine.push_back(l);
            }
    const size_t n = stStation.size();

    struct Arc {
        uint32_t to;
        float    cost;
    };
    std::vector<std::vector<Arc>> g(n);
    for (uint32_t l = 0; l < L; ++l) {
        const auto& st    = m.lines[l].stations;
        const float total = r.cum[l].back();
        const size_t k    = st.size();
        for (size_t i = 0; i < k; ++i) {
            if (!m.lines[l].loop && i + 1 == k) break;
            const uint32_t a = st[i], b = st[(i + 1) % k];
            if (a == b) continue;
            float d = std::abs(r.arc[size_t(l) * S + b] - r.arc[size_t(l) * S + a]);
            if (m.lines[l].loop) d = std::min(d, total - d);
            const float    c  = d / kTrainSpeed + kRailDwell;
            const uint32_t sa = uint32_t(stateOf[size_t(l) * S + a]), sb = uint32_t(stateOf[size_t(l) * S + b]);
            g[sa].push_back({sb, c});
            g[sb].push_back({sa, c});
        }
    }
    std::vector<std::vector<uint32_t>> statesAt(S);
    for (uint32_t i = 0; i < n; ++i) statesAt[stStation[i]].push_back(i);
    for (const auto& ss : statesAt)
        for (uint32_t a : ss)
            for (uint32_t b : ss)
                if (a != b) g[a].push_back({b, kTransferTime});

    // Dijkstra from every station; store total time and the first leg.
    r.table.assign(size_t(S) * S * 2, 0);
    std::vector<float>   dist(n);
    std::vector<int32_t> pred(n);
    using Item = std::pair<float, uint32_t>;
    for (uint32_t o = 0; o < S; ++o) {
        std::fill(dist.begin(), dist.end(), kInf);
        std::fill(pred.begin(), pred.end(), -1);
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        for (uint32_t s : statesAt[o]) {
            dist[s] = 0.0f;
            pq.push({0.0f, s});
        }
        while (!pq.empty()) {
            const auto [d, u] = pq.top();
            pq.pop();
            if (d > dist[u]) continue;
            for (const Arc& e : g[u])
                if (d + e.cost < dist[e.to]) {
                    dist[e.to] = d + e.cost;
                    pred[e.to] = int32_t(u);
                    pq.push({dist[e.to], e.to});
                }
        }

        std::vector<uint32_t> chain;
        for (uint32_t t = 0; t < S; ++t) {
            uint32_t* out = &r.table[(size_t(o) * S + t) * 2];
            float     best = kInf;
            int32_t   bs   = -1;
            for (uint32_t s : statesAt[t])
                if (dist[s] < best) {
                    best = dist[s];
                    bs   = int32_t(s);
                }
            if (t == o || bs < 0 || best == kInf) {
                out[0] = bits(t == o ? 0.0f : 1e30f);
                out[1] = kNone;
                continue;
            }
            chain.clear();
            for (int32_t x = bs; x >= 0; x = pred[size_t(x)]) chain.push_back(uint32_t(x));
            std::reverse(chain.begin(), chain.end());
            const uint32_t line   = stLine[chain[0]];
            uint32_t       alight = t;
            for (size_t k = 1; k < chain.size(); ++k)
                if (stStation[chain[k]] == stStation[chain[k - 1]]) {
                    alight = stStation[chain[k - 1]];
                    break;
                }
            out[0] = bits(best);
            out[1] = (line << 16) | alight;
        }
    }
    buildTimetables(m, r);
    return r;
}

} // namespace

SimWorld buildSimWorld(const CityMap& m) {
    const uint32_t S = uint32_t(m.stations.size()), L = uint32_t(m.lines.size());
    const uint32_t B = uint32_t(m.blocks.size());

    std::vector<uint32_t> residential, commercial, pay[3];
    auto add = [](std::vector<uint32_t>& v, uint32_t b, int n) { v.insert(v.end(), size_t(n), b); };
    for (uint32_t b = 0; b < B; ++b) {
        const DistrictKind d = m.blocks[b].district < m.districts.size() ? m.districts[m.blocks[b].district].kind
                                                                         : DistrictKind::Residential;
        switch (m.blocks[b].type) {
        case BlockType::Residential: add(residential, b, 2); break;
        case BlockType::ResidentialPoor: add(residential, b, 3); break;
        case BlockType::ResidentialRich: add(residential, b, 1); break;
        case BlockType::Commercial:
            commercial.push_back(b);
            add(pay[0], b, 1);
            if (d == DistrictKind::Business || d == DistrictKind::Wealthy) add(pay[1], b, 1);
            break;
        case BlockType::Office:
            if (d == DistrictKind::Business) {
                add(pay[2], b, 2);
                add(pay[1], b, 2);
            } else {
                add(pay[1], b, 2);
                add(pay[0], b, 1);
            }
            break;
        case BlockType::Industrial:
            add(pay[0], b, 3);
            add(pay[1], b, 1);
            if (b % 4 == 0) add(pay[2], b, 1);
            break;
        case BlockType::Park: break;
        }
    }
    if (residential.empty())
        for (uint32_t b = 0; b < B; ++b) residential.push_back(b);
    std::vector<uint32_t> anyWork;
    for (const auto& p : pay) anyWork.insert(anyWork.end(), p.begin(), p.end());
    if (anyWork.empty()) anyWork = residential;
    for (auto& p : pay)
        if (p.empty()) p = anyWork; // never leave a pay level without jobs

    std::vector<uint32_t> blockData(size_t(B) * 4);
    for (uint32_t b = 0; b < B; ++b) {
        const Vec2 c = m.blocks[b].centroid;

        uint32_t station = kNone;
        float    best    = kInf;
        for (uint32_t s = 0; s < S; ++s) {
            const float d = length2(m.stations[s].pos - c);
            if (d < best) {
                best    = d;
                station = s;
            }
        }

        uint32_t shop = b;
        const BlockType bt = m.blocks[b].type;
        if (bt == BlockType::Residential || bt == BlockType::ResidentialPoor || bt == BlockType::ResidentialRich) {
            best = kInf;
            for (uint32_t k : commercial) {
                const float d = length2(m.blocks[k].centroid - c);
                if (d < best) {
                    best = d;
                    shop = k;
                }
            }
        }
        blockData[size_t(b) * 4 + 0] = m.blocks[b].anchor;
        blockData[size_t(b) * 4 + 1] = station;
        blockData[size_t(b) * 4 + 2] = shop;
        blockData[size_t(b) * 4 + 3] = uint32_t(m.blocks[b].type);
    }

    const Rail rail = buildRail(m);

    SimWorld w;
    w.residentialCount = uint32_t(residential.size());
    w.workCount        = 0;
    for (int i = 0; i < 3; ++i) {
        w.workByPay[i] = uint32_t(pay[i].size());
        w.workCount += w.workByPay[i];
    }
    w.stationCount     = S;
    w.lineCount        = L;
    w.occupancyCount   = rail.occupancyCount;
    w.trainSlots       = rail.trainSlots;
    auto& d            = w.data;
    auto  begin        = [&](Section s) { w.sec[size_t(s)] = uint32_t(d.size()); };

    begin(kSecNodePos);
    for (const Vec2& p : m.nodes) d.insert(d.end(), {bits(p.x), bits(p.y)});
    begin(kSecAdjOff);
    d.insert(d.end(), m.adjOffsets.begin(), m.adjOffsets.end());
    begin(kSecAdj);
    for (size_t k = 0; k < m.adj.size(); ++k)
        d.push_back(m.adj[k] | (m.edges[m.adjEdge[k]].type == RoadType::Arterial ? 0x80000000u : 0u));
    begin(kSecBlocks);
    d.insert(d.end(), blockData.begin(), blockData.end());
    begin(kSecPick);
    d.insert(d.end(), residential.begin(), residential.end());
    for (const auto& p : pay) d.insert(d.end(), p.begin(), p.end());

    begin(kSecRailPts);
    std::vector<uint32_t> lineInfo;
    uint32_t              first = 0;
    for (uint32_t l = 0; l < L; ++l) {
        for (size_t i = 0; i < rail.poly[l].size(); ++i)
            d.insert(d.end(), {bits(rail.poly[l][i].x), bits(rail.poly[l][i].y), bits(rail.cum[l][i]), 0u});
        const uint32_t count = uint32_t(rail.poly[l].size());
        lineInfo.insert(lineInfo.end(), {first, count, m.lines[l].loop ? 1u : 0u, bits(rail.cum[l].back())});
        first += count;
    }
    begin(kSecLineInfo);
    d.insert(d.end(), lineInfo.begin(), lineInfo.end());
    begin(kSecStationInfo);
    for (const Station& s : m.stations) d.insert(d.end(), {s.node, bits(s.pos.x), bits(s.pos.y), 0u});
    begin(kSecRailTable);
    d.insert(d.end(), rail.table.begin(), rail.table.end());
    begin(kSecLineDir);
    d.insert(d.end(), rail.lineDir.begin(), rail.lineDir.end());
    begin(kSecProfile);
    d.insert(d.end(), rail.profile.begin(), rail.profile.end());
    begin(kSecProfileIndex);
    d.insert(d.end(), rail.profileIndex.begin(), rail.profileIndex.end());
    if (d.empty()) d.push_back(0); // never allocate an empty buffer
    return w;
}
