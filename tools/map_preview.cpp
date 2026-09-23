// Writes a generated city as SVG, no OpenGL needed. Usage: map_preview [seed] [out.svg]
#include "city/mapgen.hpp"
#include "core/palette.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr float kPx = 2048.0f;

struct Svg {
    std::FILE* f;
    float      s; // px per metre
    float      size;
    float      x(Vec2 p) const { return p.x * s; }
    float      y(Vec2 p) const { return kPx - p.y * s; }
};

const char* blockColor(BlockType t) {
    static char buf[7][8];
    const uint32_t c[] = {palette::kResidential, palette::kCommercial, palette::kOffice, palette::kIndustrial,
                          palette::kPark, palette::kResidentialPoor, palette::kResidentialRich};
    const int i = int(t);
    std::snprintf(buf[i], sizeof buf[i], "#%06x", c[i]);
    return buf[i];
}

} // namespace

int main(int argc, char** argv) {
    const uint32_t seed = argc > 1 ? uint32_t(std::strtoul(argv[1], nullptr, 10)) : 1u;
    const char*    out  = argc > 2 ? argv[2] : "city.svg";

    const auto    t0 = std::chrono::steady_clock::now();
    const CityMap m  = generateCity(seed, 20'000.0f);
    const double  ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    size_t arterials = 0;
    for (const Edge& e : m.edges) arterials += e.type == RoadType::Arterial;
    std::printf("seed %u: %.0f ms | nodes %zu | edges %zu (%zu arterial) | blocks %zu | lines %zu | stations %zu\n",
                seed, ms, m.nodes.size(), m.edges.size(), arterials, m.blocks.size(), m.lines.size(),
                m.stations.size());

    std::FILE* file = std::fopen(out, "w");
    if (!file) return 1;
    const Svg s{file, kPx / m.size, m.size};

    std::fprintf(file, "<svg xmlns='http://www.w3.org/2000/svg' width='%.0f' height='%.0f'>\n", kPx, kPx);
    std::fprintf(file, "<rect width='100%%' height='100%%' fill='#%06x'/>\n", palette::kBackground);

    if (!m.water.empty()) {
        std::fprintf(file, "<path fill='#%06x' d='", palette::kWater);
        for (size_t i = 1; i < m.water.size(); ++i)
            std::fprintf(file, "%c%.1f %.1f ", i == 1 ? 'M' : 'L', s.x(m.water[i]), s.y(m.water[i]));
        std::fprintf(file, "L%.1f %.1fZ'/>\n", s.x(m.water[0]), s.y(m.water[0]));
    }

    for (const Block& b : m.blocks) {
        std::fprintf(file, "<path fill='%s' d='", blockColor(b.type));
        for (size_t i = 0; i < b.poly.size(); ++i)
            std::fprintf(file, "%c%.1f %.1f ", i ? 'L' : 'M', s.x(b.poly[i]), s.y(b.poly[i]));
        std::fprintf(file, "Z'/>\n");
    }

    for (RoadType t : {RoadType::Local, RoadType::Arterial}) {
        const bool art = t == RoadType::Arterial;
        std::fprintf(file, "<path fill='none' stroke='#%06x' stroke-width='%.2f' stroke-linecap='round' d='",
                     art ? palette::kArterial : palette::kLocalRoad, art ? 1.6 : 0.6);
        for (const Edge& e : m.edges)
            if (e.type == t)
                std::fprintf(file, "M%.1f %.1f L%.1f %.1f ", s.x(m.nodes[e.a]), s.y(m.nodes[e.a]),
                             s.x(m.nodes[e.b]), s.y(m.nodes[e.b]));
        std::fprintf(file, "'/>\n");
    }

    for (size_t li = 0; li < m.lines.size(); ++li) {
        const RailLine& l = m.lines[li];
        std::fprintf(file, "<path fill='none' stroke='#%06x' stroke-width='2.5' stroke-linejoin='round' d='",
                     palette::kRail[li % palette::kRailCount]);
        for (size_t i = 0; i < l.path.size(); ++i)
            std::fprintf(file, "%c%.1f %.1f ", i ? 'L' : 'M', s.x(l.path[i]), s.y(l.path[i]));
        std::fprintf(file, "%s'/>\n", l.loop ? "Z" : "");
    }

    for (const Station& st : m.stations)
        std::fprintf(file, "<rect x='%.1f' y='%.1f' width='7' height='7' fill='#%06x' stroke='#%06x'/>\n",
                     s.x(st.pos) - 3.5f, s.y(st.pos) - 3.5f, palette::kStation, palette::kBackground);

    std::fprintf(file, "</svg>\n");
    std::fclose(file);
    std::printf("wrote %s\n", out);
    return 0;
}
