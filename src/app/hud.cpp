#include "app/hud.hpp"

#include "core/palette.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

using namespace palette;

namespace {

std::vector<std::string> wrap(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string              line, word;
    auto                     flush = [&] {
        if (!line.empty()) lines.push_back(line);
        line.clear();
    };
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ' ') {
            if (!line.empty() && line.size() + 1 + word.size() > width) flush();
            line += (line.empty() ? "" : " ") + word;
            word.clear();
        } else {
            word += text[i];
        }
    }
    flush();
    return lines.empty() ? std::vector<std::string>{""} : lines;
}

float legend(Ui& ui, float x, float y, const char* lo, const char* hi, std::initializer_list<uint32_t> colors,
             float swatch, float opacity) {
    x              = ui.text(x + ui.charWidth(), y, lo, kComment);
    const float sw = ui.charWidth() * swatch, sh = ui.lineHeight() * 0.5f;
    for (uint32_t c : colors) {
        ui.rect(x, y + ui.lineHeight() * 0.28f, sw, sh, c, opacity);
        x += sw;
    }
    return ui.text(x + ui.charWidth() * 0.5f, y, hi, kComment);
}

const char* layerName(uint32_t mode) {
    switch (mode) {
    case HeatMap::kOutdoors: return "outdoors";
    case HeatMap::kEveryone: return "everyone";
    case HeatMap::kTraffic:  return "traffic";
    default:                 return "off";
    }
}

} // namespace

std::string human(uint32_t v) {
    char b[16];
    if (v >= 1'000'000) std::snprintf(b, sizeof b, "%.1fM", v / 1e6);
    else if (v >= 10'000) std::snprintf(b, sizeof b, "%uk", v / 1000);
    else std::snprintf(b, sizeof b, "%u", v);
    return b;
}

float drawHud(Ui& ui, const App& a, const HudInfo& h) {
    const float pad = std::round(ui.lineHeight() * 0.45f), height = ui.lineHeight() + 2.0f * pad;
    const float gap = ui.charWidth() * 2.5f, y = pad;
    ui.rect(0, 0, float(a.fbW), height, kBar, 0.94f);

    const uint64_t t   = uint64_t(h.simMs) + Agents::kDayOffset;
    const uint64_t tod = t / 60000 % 1440;
    char           b[32];
    std::snprintf(b, sizeof b, "%02u:%02u", unsigned(tod / 60), unsigned(tod % 60));
    float x = ui.text(gap * 0.6f, y, b, kFg);
    x       = ui.text(x + ui.charWidth(), y, "day " + std::to_string(t / 86'400'000 + 1), kComment) + gap;

    if (a.paused) {
        x = ui.text(x, y, "PAUSED", kYellow);
    } else {
        std::snprintf(b, sizeof b, "x%g", double(a.timeScale));
        x = ui.text(ui.text(x, y, "speed ", kComment), y, b, kGreen);
        if (h.carLimited) x = ui.text(x + ui.charWidth(), y, "(car-limited)", kRed);
    }
    x += gap;

    auto item = [&](const char* label, uint32_t value, uint32_t color) {
        x = ui.text(x, y, std::string(label) + " ", kComment);
        x = ui.text(x, y, human(value), color) + ui.charWidth() * 1.5f;
    };
    item("people", h.people, kFg);
    item("walking", h.stats.walking, kBlue);
    item("platforms", h.stats.waiting, kCyan);
    item("trains", h.stats.riding, kGreen);
    item("driving", h.stats.driving, kOrange);
    item("home", h.stats.home, kFgDim);
    item("work", h.stats.work, kFgDim);
    item("shopping", h.stats.shop, kMagenta);
    x += gap - ui.charWidth() * 1.5f;
    item("cars", h.stats.carsOnRoad, kFg);
    item("stopped", h.stats.carsStopped, kRed);

    x += gap - ui.charWidth() * 1.5f;
    x = ui.text(x, y, "density ", kComment);
    x = ui.text(x, y, layerName(a.heatMode), a.heatMode == HeatMap::kOff ? kComment : kFgDim);
    if (h.layerOpacity > 0.0f) {
        if (a.heatMode == HeatMap::kTraffic)
            legend(ui, x, y, "free ", "jam", {kGreen, kYellow, kRed}, 1.8f, h.layerOpacity);
        else
            legend(ui, x, y, "low ", "high", {0x3d59a1, 0x7aa2f7, 0xbb9af7, 0xf7768e, 0xff9e64, 0xe0af68}, 1.2f,
                   h.layerOpacity);
    }

    std::snprintf(b, sizeof b, "%.0f fps", h.fps);
    ui.text(float(a.fbW) - ui.textWidth(b) - gap * 0.6f, y, b, kComment);
    return height;
}

void drawPanel(Ui& ui, App& a, const Panel& p, float top) {
    const float  lh = ui.lineHeight(), cw = ui.charWidth(), pad = std::round(lh * 0.7f);
    const size_t labelChars = 9, valueChars = 44;

    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto& [label, value] : p.rows) {
        const auto parts = wrap(value, valueChars);
        for (size_t i = 0; i < parts.size(); ++i) lines.push_back({i == 0 ? label : "", parts[i]});
    }
    const float w  = 2.0f * pad + cw * float(labelChars + valueChars);
    const float h  = 2.0f * pad + lh * (2.6f + float(lines.size()) + 1.4f);
    const float x0 = pad, y0 = top + pad;
    ui.rect(x0, y0, w, h, kPanel, 0.95f);
    ui.rect(x0, y0, std::round(cw * 0.35f), h, kYellow);

    float y = y0 + pad;
    ui.text(x0 + pad, y, p.title, kFg);
    y += lh;
    ui.text(x0 + pad, y, p.subtitle, kFgDim);
    y += lh * 1.6f;
    for (const auto& [label, value] : lines) {
        ui.text(x0 + pad, y, label, kComment);
        ui.text(x0 + pad + cw * float(labelChars), y, value, kFgDim);
        y += lh;
    }
    y += lh * 0.4f;
    ui.text(x0 + pad, y, a.follow ? "following  F: stop  Esc: close" : "F: follow  Esc: close",
            a.follow ? kYellow : kComment);

    a.panel[0] = x0;
    a.panel[1] = y0;
    a.panel[2] = x0 + w;
    a.panel[3] = y0 + h;
}

void drawTrail(Ui& ui, const App& a, const std::vector<Vec2>& trail) {
    const float size = std::max(3.0f, std::round(ui.lineHeight() * 0.2f));
    for (size_t i = 0; i < trail.size(); ++i) {
        const float sx = 0.5f * float(a.fbW) + (trail[i].x - a.cam.cx) * a.cam.ppm;
        const float sy = 0.5f * float(a.fbH) - (trail[i].y - a.cam.cy) * a.cam.ppm;
        if (sx < -size || sy < -size || sx > float(a.fbW) + size || sy > float(a.fbH) + size) continue;
        const float age = float(i + 1) / float(trail.size());
        ui.rect(sx - 0.5f * size, sy - 0.5f * size, size, size, kYellow, 0.15f + 0.75f * age);
    }
}

void drawHelp(Ui& ui, const App& a) {
    const std::string help = "drag: pan  wheel: zoom  click: inspect  F: follow  P: random person  space: pause  "
                             "+/-: speed  D: layer  N: new city  H: hide UI";
    const float pad = std::round(ui.lineHeight() * 0.45f);
    const float w = ui.textWidth(help) + pad * 4.0f, h = ui.lineHeight() + pad * 2.0f;
    ui.rect(float(a.fbW) - w, float(a.fbH) - h, w, h, kBar, 0.85f);
    ui.text(float(a.fbW) - w + pad * 2.0f, float(a.fbH) - h + pad, help, kComment);
}
