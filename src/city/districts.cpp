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


// Seeds on a jittered 1.6 km grid; each block joins the nearest seed, so borders
// follow streets. Business near the centre / main hub, industry by the bay; the rest
// is ranked by a prestige score (ring around the centre, west side, noise, minus
// nearby industry and water): top 14% wealthy, bottom 16% poor.
void buildDistricts(CityMap& m, const Field& f, Vec2 C, float R, const std::vector<Vec2>& hubs, Rng& rng) {
    const float step = 1600.0f;
    const int   n    = int(m.size / step);
    for (int gy = 0; gy < n; ++gy)
        for (int gx = 0; gx < n; ++gx) {
            const Vec2 p{(float(gx) + 0.5f + rng.uni(-0.35f, 0.35f)) * step,
                         (float(gy) + 0.5f + rng.uni(-0.35f, 0.35f)) * step};
            if (!f.water(p)) m.districts.push_back({p, DistrictKind::Residential, {}});
        }

    auto nearWater = [&](Vec2 p, float r) {
        for (int k = 0; k < 12; ++k) {
            const float a = float(k) * kPi / 6.0f;
            if (f.water(p + Vec2{std::cos(a), std::sin(a)} * r) || f.water(p + Vec2{std::cos(a), std::sin(a)} * r * 0.5f))
                return true;
        }
        return false;
    };

    std::vector<size_t> open;
    for (size_t i = 0; i < m.districts.size(); ++i) {
        District& d  = m.districts[i];
        const float dc = length(d.seed - C);
        if ((dc < 0.55f * R) || (!hubs.empty() && length(d.seed - hubs[0]) < 1300.0f)) d.kind = DistrictKind::Business;
        else if ((nearWater(d.seed, 1400.0f) && rng.uni() < 0.75f) || (dc > 1.9f * R && rng.uni() < 0.10f))
            d.kind = DistrictKind::Industrial;
        else open.push_back(i);
    }

    std::vector<std::pair<float, size_t>> ranked;
    for (size_t i : open) {
        const Vec2  p  = m.districts[i].seed;
        const float dc = length(p - C);
        float nearestIndustry = kInf;
        for (const District& o : m.districts)
            if (o.kind == DistrictKind::Industrial) nearestIndustry = std::min(nearestIndustry, length(o.seed - p));
        float prestige = 0.6f * fbm(p * (1.0f / 5000.0f), f.seed + 71u);
        prestige += 0.25f * std::clamp(1.0f - std::abs(dc - 1.1f * R) / (1.2f * R), 0.0f, 1.0f);
        prestige += 0.15f * dot(normalize(p - C), normalize(Vec2{-1.0f, 0.3f}));
        prestige -= 0.40f * std::clamp(1.0f - nearestIndustry / 2500.0f, 0.0f, 1.0f);
        prestige -= nearWater(p, 1200.0f) ? 0.2f : 0.0f;
        ranked.emplace_back(prestige, i);
    }
    std::sort(ranked.begin(), ranked.end());
    const size_t poor = ranked.size() * 16 / 100, rich = ranked.size() * 14 / 100;
    for (size_t k = 0; k < ranked.size(); ++k)
        m.districts[ranked[k].second].kind = k < poor                   ? DistrictKind::Poor
                                           : k >= ranked.size() - rich ? DistrictKind::Wealthy
                                                                        : DistrictKind::Residential;

    static const char* kPlace[] = {"Aoyama", "Akasaka", "Kanda", "Asakusa", "Ueno", "Meguro", "Nakano", "Suginami",
                                   "Nerima", "Itabashi", "Kita", "Adachi", "Katsushika", "Edogawa", "Koto",
                                   "Minato", "Chuo", "Bunkyo", "Toshima", "Setagaya", "Ota", "Shinagawa",
                                   "Sumida", "Arakawa", "Daikanyama", "Ebisu", "Hiroo", "Azabu", "Yanaka",
                                   "Kichijoji", "Koenji", "Ogikubo", "Oji", "Senju", "Kameido", "Kiba",
                                   "Tsukishima", "Monzen", "Ningyocho", "Kagurazaka"};
    static const char* kKind[] = {"business district", "residential", "residential (wealthy)",
                                  "residential (poor)", "industrial zone"};
    for (size_t i = 0; i < m.districts.size(); ++i) {
        const char* place = kPlace[i % std::size(kPlace)];
        const char* dir[] = {"", "Higashi-", "Nishi-", "Kita-", "Minami-", "Shin-"};
        m.districts[i].name = std::string(dir[(i / std::size(kPlace)) % 6]) + place + " " + kKind[int(m.districts[i].kind)];
    }
}

namespace {

uint16_t districtOf(const CityMap& m, Vec2 p) {
    uint16_t best = 0;
    float    bd   = kInf;
    for (size_t i = 0; i < m.districts.size(); ++i) {
        const float d = length2(m.districts[i].seed - p);
        if (d < bd) {
            bd   = d;
            best = uint16_t(i);
        }
    }
    return best;
}

BlockType classify(const CityMap& m, const Field& f, Vec2 c, DistrictKind district, Rng& rng) {
    if (length(c - f.palace) < f.palaceRadius) return BlockType::Park;

    float ds = kInf;
    for (const Station& s : m.stations) ds = std::min(ds, length2(s.pos - c));
    ds = std::sqrt(ds);

    const float r = rng.uni();
    switch (district) {
    case DistrictKind::Business:
        if (ds < 200.0f) return BlockType::Commercial;
        return r < 0.72f ? BlockType::Office : r < 0.95f ? BlockType::Commercial : BlockType::Park;
    case DistrictKind::Industrial:
        return r < 0.85f ? BlockType::Industrial : r < 0.95f ? BlockType::ResidentialPoor : BlockType::Commercial;
    case DistrictKind::Wealthy:
        if (ds < 150.0f) return BlockType::Commercial;
        return r < 0.84f ? BlockType::ResidentialRich : r < 0.96f ? BlockType::Park : BlockType::Commercial;
    case DistrictKind::Poor:
        if (ds < 150.0f) return BlockType::Commercial;
        return r < 0.86f ? BlockType::ResidentialPoor : r < 0.94f ? BlockType::Commercial : BlockType::Industrial;
    case DistrictKind::Residential:
        if (ds < 200.0f) return r < 0.8f ? BlockType::Commercial : BlockType::Office;
        return r < 0.86f ? BlockType::Residential : r < 0.93f ? BlockType::Commercial
             : r < 0.97f ? BlockType::Park : BlockType::Office;
    }
    return BlockType::Residential;
}

} // namespace

void extractBlocks(CityMap& m, const Field& f, Rng& rng) {
    const auto& off = m.adjOffsets;
    struct Face {
        uint32_t begin, end;
        float    area;
    };
    std::vector<uint8_t>  used(m.adj.size(), 0);
    std::vector<uint32_t> fn, fe;
    std::vector<Face>     faces;
    std::vector<Vec2>     poly;

    // Faces of the planar graph: walk every half-edge once, at each node turning to
    // the next neighbour clockwise from the one we came from.
    for (uint32_t u = 0; u < m.nodes.size(); ++u)
        for (uint32_t s = off[u]; s < off[u + 1]; ++s) {
            if (used[s]) continue;
            const uint32_t begin = uint32_t(fn.size());
            uint32_t       cu = u, cs = s;
            while (!used[cs]) {
                used[cs] = 1;
                fn.push_back(cu);
                fe.push_back(m.adjEdge[cs]);
                const uint32_t v    = m.adj[cs];
                uint32_t       back = off[v];
                while (m.adj[back] != cu) ++back;
                const uint32_t deg = off[v + 1] - off[v];
                cs                 = off[v] + (back - off[v] + deg - 1) % deg;
                cu                 = v;
            }
            poly.clear();
            for (uint32_t k = begin; k < fn.size(); ++k) poly.push_back(m.nodes[fn[k]]);
            faces.push_back({begin, uint32_t(fn.size()), shoelace(poly)});
        }

    // Interior faces share one orientation; the outer boundary has the other.
    size_t pos = 0;
    for (const Face& fc : faces) pos += fc.area > 0.0f;
    const float sign = pos * 2 >= faces.size() ? 1.0f : -1.0f;

    for (const Face& fc : faces) {
        const float area = fc.area * sign;
        const size_t cnt = fc.end - fc.begin;
        if (area < 150.0f || area > 4e6f || cnt > 64) continue;

        poly.clear();
        for (uint32_t k = fc.begin; k < fc.end; ++k) poly.push_back(m.nodes[fn[k]]);

        Vec2 c{};
        for (size_t i = 0; i < cnt; ++i) {
            const Vec2 a = poly[i], b = poly[(i + 1) % cnt];
            c            = c + (a + b) * cross(a, b);
        }
        c = c / (6.0f * fc.area);

        // Corners move towards the centroid by half the road width only: this is not a
        // true edge offset, and the sidewalks drawn on top hide the unevenness.
        Block blk;
        for (size_t i = 0; i < cnt; ++i) {
            const RoadType tPrev = m.edges[fe[fc.begin + (i + cnt - 1) % cnt]].type;
            const RoadType tNext = m.edges[fe[fc.begin + i]].type;
            const float    s     = 0.5f * std::max(roadWidth(tPrev), roadWidth(tNext));
            const Vec2     d     = poly[i] - c;
            const float    l     = length(d);
            blk.poly.push_back(c + d * std::max(0.0f, 1.0f - s / std::max(l, 1e-3f)));
        }
        blk.area = std::abs(shoelace(blk.poly));
        if (blk.area < 60.0f) continue;

        blk.centroid = c;
        float bestD  = kInf;
        for (uint32_t k = fc.begin; k < fc.end; ++k) {
            const float dd = length2(m.nodes[fn[k]] - c);
            if (dd < bestD) {
                bestD      = dd;
                blk.anchor = fn[k];
            }
        }
        blk.district = districtOf(m, c);
        blk.type     = classify(m, f, c, m.districts[blk.district].kind, rng);
        m.blocks.push_back(std::move(blk));
    }
}



void nameStations(CityMap& m, Rng& rng) {
    static const char* kPrefix[] = {"", "", "", "", "Shin-", "Higashi-", "Nishi-", "Kita-", "Minami-", "Kami-", "Shimo-"};
    static const char* kRoot[]   = {"Aoba", "Sakura", "Midori", "Kawa", "Hama", "Oka", "Mori", "Ike", "Sawa", "Hara",
                                    "Yama", "Shima", "Matsu", "Take", "Ume", "Kiri", "Hoshi", "Tsuki", "Kaze", "Nami",
                                    "Hana", "Ishi", "Kusa", "Fuji", "Tama", "Asa", "Yuki", "Kane", "Sora", "Kumo",
                                    "Taki", "Nagi", "Hibari", "Tsubaki", "Kaede", "Suzu"};
    static const char* kSuffix[] = {"", "", "-machi", "-bashi", "dai", "-cho", "gaoka", "zaka", "no", "mon", "-koen",
                                    "gawa", "hara", "ya", "ta", "shima"};
    std::vector<std::string> used;
    for (Station& s : m.stations) {
        for (int attempt = 0; attempt < 100 && s.name.empty(); ++attempt) {
            std::string n = std::string(kPrefix[rng.below(int(std::size(kPrefix)))]) +
                            kRoot[rng.below(int(std::size(kRoot)))] + kSuffix[rng.below(int(std::size(kSuffix)))];
            if (std::find(used.begin(), used.end(), n) == used.end()) s.name = n;
        }
        if (s.name.empty()) s.name = "Station " + std::to_string(used.size() + 1);
        used.push_back(s.name);
    }
    for (size_t l = 0; l < m.lines.size(); ++l) {
        RailLine& line = m.lines[l];
        if (line.loop) line.name = "Ring Line";
        else if (l == 1) line.name = "Chuo Line";
        else if (line.stations.empty()) line.name = "Line " + std::to_string(l);
        else line.name = m.stations[line.stations.back()].name + " Line";
    }
}


} // namespace citygen
