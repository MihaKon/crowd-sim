#pragma once
#include "app/app.hpp"
#include "core/math.hpp"
#include "sim/agents.hpp"
#include "ui/inspector.hpp"
#include "ui/ui.hpp"

#include <string>
#include <vector>

struct HudInfo {
    double        simMs = 0.0, fps = 0.0;
    bool          carLimited = false;
    float         layerOpacity = 0.0f;
    uint32_t      people = 0;
    Agents::Stats stats;
};

// "1.2M", "845k", "37"
std::string human(uint32_t v);

// Top bar: clock, speed, where people are, cars, active layer, fps. Returns its height.
float drawHud(Ui& ui, const App& a, const HudInfo& h);
// Inspector panel under the bar; stores its rectangle in `a` so clicks on it don't pick.
void drawPanel(Ui& ui, App& a, const Panel& p, float top);
// Breadcrumbs of the followed person / car, older dots fainter.
void drawTrail(Ui& ui, const App& a, const std::vector<Vec2>& trail);
void drawHelp(Ui& ui, const App& a);
