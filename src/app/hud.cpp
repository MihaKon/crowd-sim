#include "app/hud.hpp"

#include "core/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

using namespace palette::ui;

namespace {

struct Metrics {
    float lh, cw, pad, radius, margin;
    explicit Metrics(const Ui& ui)
        : lh(ui.lineHeight()), cw(ui.charWidth()), pad(std::round(ui.lineHeight() * 0.8f)),
          radius(std::round(ui.lineHeight() * 0.6f)), margin(std::round(ui.lineHeight() * 0.8f)) {}
};

void card(Ui& ui, const Metrics& m, float x, float y, float w, float h, float alpha = 1.0f) {
    ui.shadow(x, y, w, h, m.radius, m.lh * 0.9f, 0.35f * alpha);
    ui.roundRect(x, y, w, h, m.radius, kCard, kCardA * alpha);
}

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

uint32_t mixRgb(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) {
        const float x = float((a >> s) & 0xFFu), y = float((b >> s) & 0xFFu);
        return uint32_t(std::lround(x + (y - x) * t)) << s;
    };
    return ch(16) | ch(8) | ch(0);
}

// A horizontal colour ramp of evenly spaced stops.
void ramp(Ui& ui, float x, float y, float w, float h, const uint32_t* stops, int n, float alpha) {
    const int slices = 48;
    for (int i = 0; i < slices; ++i) {
        const float t = (float(i) + 0.5f) / float(slices) * float(n - 1);
        const int   k = std::min(int(t), n - 2);
        ui.rect(x + w * float(i) / float(slices), y, std::ceil(w / float(slices)) + 0.5f, h,
                mixRgb(stops[k], stops[k + 1], t - float(k)), alpha);
    }
}

// Key cap and its label; returns the x after it.
float keyHint(Ui& ui, const Metrics& m, float x, float y, const char* key, const char* label, bool draw) {
    const float kw = ui.textWidth(key) + m.cw * 1.2f, kh = m.lh * 1.25f;
    if (draw) {
        ui.roundRect(x, y - m.lh * 0.125f, kw, kh, m.radius * 0.45f, kKey, 0.95f);
        ui.text(x + m.cw * 0.6f, y, key, kText);
        ui.text(x + kw + m.cw * 0.6f, y, label, kFaint);
    }
    return x + kw + m.cw * 0.6f + ui.textWidth(label) + m.cw * 1.8f;
}

const char* layerTitle(uint32_t mode) {
    switch (mode) {
    case HeatMap::kOutdoors: return "People outdoors";
    case HeatMap::kEveryone: return "Everyone";
    case HeatMap::kTraffic:  return "Traffic";
    default:                 return "No layer";
    }
}

// Sun or moon, from the hour.
void skyIcon(Ui& ui, float cx, float cy, float r, float hour) {
    const bool day = hour >= 5.25f && hour < 18.75f;
    if (day) {
        ui.circle(cx, cy, r * 1.45f, kAccent, 0.18f);
        ui.circle(cx, cy, r, kAccent);
    } else {
        ui.circle(cx, cy, r, 0xdde3ee);
        ui.circle(cx + r * 0.45f, cy - r * 0.3f, r * 0.85f, kCard); // crescent
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

float drawHud(Ui& ui, App& a, const HudInfo& h) {
    const Metrics m(ui);
    const float   W = float(a.fbW), H = float(a.fbH);

    // ---- clock: sky icon, time, day and speed, a 24 h timeline
    const uint64_t t    = uint64_t(h.simMs) + Agents::kDayOffset;
    const uint64_t tod  = t / 60000 % 1440;
    const float    hour = float(tod) / 60.0f;
    char           clock[16], speed[32];
    std::snprintf(clock, sizeof clock, "%02u:%02u", unsigned(tod / 60), unsigned(tod % 60));
    std::snprintf(speed, sizeof speed, "x%g", double(a.timeScale));
    const std::string day = "Day " + std::to_string(t / 86'400'000 + 1);

    const float lhL   = ui.lineHeight(Ui::kLarge);
    const float icon  = lhL * 0.3f;
    const float lineW = std::max(ui.textWidth(clock, Ui::kLarge) + icon * 2.0f + m.cw * 1.5f,
                                 ui.textWidth(day + "   " + (a.paused ? "Paused" : speed) + "  car-limited"));
    const float cx = m.margin, cy = m.margin, cw = lineW + 2.0f * m.pad;
    const float ch = 2.0f * m.pad + lhL + m.lh * 1.9f;
    card(ui, m, cx, cy, cw, ch);
    float y = cy + m.pad;
    skyIcon(ui, cx + m.pad + icon, y + lhL * 0.52f, icon, hour);
    ui.text(cx + m.pad + icon * 2.0f + m.cw * 1.2f, y, clock, kText, 1.0f, Ui::kLarge);
    y += lhL + m.lh * 0.1f;
    float x = ui.text(cx + m.pad, y, day, kDim) + m.cw * 3.0f;
    if (a.paused) {
        ui.text(x, y, "Paused", kAccent);
    } else {
        x = ui.text(x, y, speed, kText);
        if (h.carLimited) ui.text(x + m.cw * 2.0f, y, "car-limited", kStopped);
    }
    // Timeline: night, day (05:15-18:45), night, with a marker at the current time.
    const float ty = y + m.lh * 1.25f, tw = cw - 2.0f * m.pad, th = std::max(3.0f, std::round(m.lh * 0.22f));
    const float d0 = tw * 5.25f / 24.0f, d1 = tw * 18.75f / 24.0f;
    ui.roundRect(cx + m.pad, ty, tw, th, th * 0.5f, kNightBar);
    ui.rect(cx + m.pad + d0, ty, d1 - d0, th, kDayBar, 0.85f);
    ui.circle(cx + m.pad + tw * hour / 24.0f, ty + th * 0.5f, th * 1.3f, kText);
    const float bottomOfClock = cy + ch;

    // ---- stats card, top right (hidden in very narrow windows)
    const size_t labelChars = 13, valueChars = 6;
    const float  sw = 2.0f * m.pad + m.cw * float(labelChars + valueChars + 2);
    if (W > cw + sw + 3.0f * m.margin) {
        struct Row {
            const char* label;
            uint32_t    value, color;
        };
        const Row rows[] = {{"Walking", h.stats.walking, kWalk},      {"On platforms", h.stats.waiting, kPlatform},
                            {"On trains", h.stats.riding, kTrain},    {"Driving", h.stats.driving, kDrive},
                            {"At home", h.stats.home, kHome},         {"At work", h.stats.work, kWork},
                            {"Shopping", h.stats.shop, kShop}};
        const float rowH = m.lh * 1.15f;
        const float sh   = 2.0f * m.pad + m.lh * 1.5f + rowH * 7.0f + m.lh * 0.8f + rowH * 2.0f + m.lh * 1.2f;
        const float sx = W - m.margin - sw, sy = m.margin;
        card(ui, m, sx, sy, sw, sh);
        const float right = sx + sw - m.pad;
        float       ry    = sy + m.pad;
        auto value = [&](float yy, const std::string& v, uint32_t c) { ui.text(right - ui.textWidth(v), yy, v, c); };
        ui.text(sx + m.pad, ry, "People", kText);
        value(ry, human(h.people), kText);
        ry += m.lh * 1.5f;
        auto row = [&](const char* label, uint32_t v, uint32_t c) {
            ui.circle(sx + m.pad + m.lh * 0.22f, ry + m.lh * 0.55f, m.lh * 0.22f, c);
            ui.text(sx + m.pad + m.cw * 1.6f, ry, label, kDim);
            value(ry, human(v), kText);
            ry += rowH;
        };
        for (const Row& r : rows) row(r.label, r.value, r.color);
        ry += m.lh * 0.3f;
        ui.rect(sx + m.pad, ry, sw - 2.0f * m.pad, 1.0f, kFaint, 0.5f);
        ry += m.lh * 0.5f;
        row("Cars", h.stats.carsOnRoad, kText);
        row("Stopped", h.stats.carsStopped, kStopped);
        char fps[16];
        std::snprintf(fps, sizeof fps, "%.0f fps", h.fps);
        value(ry + m.lh * 0.2f, fps, kFaint);
    }

    // ---- layer legend, bottom left
    const float lw   = 2.0f * m.pad + m.cw * 24.0f;
    const float lhgt = 2.0f * m.pad + m.lh * (a.heatMode == HeatMap::kOff ? 1.0f : 2.9f);
    const float lx = m.margin, ly = H - m.margin - lhgt;
    card(ui, m, lx, ly, lw, lhgt);
    float gx = ui.text(lx + m.pad, ly + m.pad, layerTitle(a.heatMode), kText);
    ui.text(gx + m.cw, ly + m.pad, "(D)", kFaint);
    if (a.heatMode != HeatMap::kOff) {
        const bool  traffic = a.heatMode == HeatMap::kTraffic;
        const float by = ly + m.pad + m.lh * 1.35f, bw = lw - 2.0f * m.pad, bh = std::round(m.lh * 0.45f);
        if (h.layerOpacity > 0.02f) {
            ramp(ui, lx + m.pad, by, bw, bh, traffic ? kTrafficRamp : kDensityRamp, traffic ? 3 : 6, h.layerOpacity);
            const char* lo = traffic ? "free" : "few";
            const char* hi = traffic ? "jammed" : "crowded";
            ui.text(lx + m.pad, by + bh + m.lh * 0.15f, lo, kFaint);
            ui.text(lx + lw - m.pad - ui.textWidth(hi), by + bh + m.lh * 0.15f, hi, kFaint);
        } else {
            ui.text(lx + m.pad, by - m.lh * 0.1f, "zoom out to see it", kFaint);
        }
    }

    // ---- key hints, bottom right, wrapping into as many lines as needed
    static const std::pair<const char*, const char*> kHints[] = {
        {"Drag", "pan"}, {"Wheel", "zoom"},  {"Click", "inspect"}, {"F", "follow"},   {"P", "random person"},
        {"Space", "pause"}, {"+ -", "speed"}, {"D", "layer"},       {"N", "new city"}, {"H", "hide UI"},
        {"Q", "full resolution"}};
    const float avail = W - (lx + lw) - 3.0f * m.margin - 2.0f * m.pad;
    if (avail > m.cw * 16.0f) {
        std::vector<std::vector<size_t>> lines(1);
        float                            used = 0.0f, widest = 0.0f;
        for (size_t i = 0; i < std::size(kHints); ++i) {
            const float w = keyHint(ui, m, 0.0f, 0.0f, kHints[i].first, kHints[i].second, false);
            if (used + w > avail && !lines.back().empty()) {
                lines.emplace_back();
                used = 0.0f;
            }
            lines.back().push_back(i);
            used += w;
            widest = std::max(widest, used);
        }
        const float rowH = m.lh * 1.6f;
        const float hw = widest + 2.0f * m.pad - m.cw * 1.8f;
        const float hh = 2.0f * m.pad + rowH * float(lines.size()) - m.lh * 0.35f;
        const float hx = W - m.margin - hw, hy = H - m.margin - hh;
        card(ui, m, hx, hy, hw, hh, 0.9f);
        float ky = hy + m.pad + m.lh * 0.1f;
        for (const auto& line : lines) {
            float kx = hx + m.pad;
            for (size_t i : line) kx = keyHint(ui, m, kx, ky, kHints[i].first, kHints[i].second, true);
            ky += rowH;
        }
    }
    return bottomOfClock;
}

void drawPanel(Ui& ui, App& a, const Panel& p, float top) {
    const Metrics m(ui);
    const size_t  labelChars = 9, valueChars = 40;

    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto& [label, value] : p.rows) {
        const auto parts = wrap(value, valueChars);
        for (size_t i = 0; i < parts.size(); ++i) lines.push_back({i == 0 ? label : "", parts[i]});
    }
    const float w  = 2.0f * m.pad + m.cw * float(labelChars + valueChars) + m.cw;
    const float h  = 2.0f * m.pad + m.lh * (2.8f + float(lines.size()) + 1.9f);
    const float x0 = m.margin, y0 = top + m.margin * 0.75f;
    card(ui, m, x0, y0, w, h);
    ui.roundRect(x0, y0 + m.radius, std::round(m.cw * 0.35f), h - 2.0f * m.radius, m.cw * 0.2f, kAccent);

    float y = y0 + m.pad;
    ui.text(x0 + m.pad, y, p.title, kText);
    y += m.lh * 1.1f;
    ui.text(x0 + m.pad, y, p.subtitle, kDim);
    y += m.lh * 1.7f;
    for (const auto& [label, value] : lines) {
        ui.text(x0 + m.pad, y, label, kFaint);
        ui.text(x0 + m.pad + m.cw * float(labelChars), y, value, kDim);
        y += m.lh;
    }
    y += m.lh * 0.75f;
    float x = keyHint(ui, m, x0 + m.pad, y, "F", a.follow ? "stop following" : "follow", true);
    keyHint(ui, m, x, y, "Esc", "close", true);
    if (a.follow) ui.circle(x0 + w - m.pad - m.lh * 0.3f, y + m.lh * 0.5f, m.lh * 0.3f, kAccent);

    a.panel[0] = x0;
    a.panel[1] = y0;
    a.panel[2] = x0 + w;
    a.panel[3] = y0 + h;
}

void drawTrail(Ui& ui, const App& a, const std::vector<Vec2>& trail) {
    const float r = std::max(2.0f, std::round(ui.lineHeight() * 0.13f));
    for (size_t i = 0; i < trail.size(); ++i) {
        const float sx = 0.5f * float(a.fbW) + (trail[i].x - a.cam.cx) * a.cam.ppm;
        const float sy = 0.5f * float(a.fbH) - (trail[i].y - a.cam.cy) * a.cam.ppm;
        if (sx < -r || sy < -r || sx > float(a.fbW) + r || sy > float(a.fbH) + r) continue;
        const float age = float(i + 1) / float(trail.size());
        ui.circle(sx, sy, r, kAccent, 0.12f + 0.78f * age);
    }
}
