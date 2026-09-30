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

// Cards over the city: clock (top left), where people are (top right), the active
// layer's legend (bottom left) and key hints (bottom right). Returns the bottom of
// the clock card, where the inspector panel goes.
float drawHud(Ui& ui, App& a, const HudInfo& h);
// Inspector panel under the clock; stores its rectangle in `a` so clicks on it don't pick.
void drawPanel(Ui& ui, App& a, const Panel& p, float top);
// Breadcrumbs of the followed person / car, older dots fainter.
void drawTrail(Ui& ui, const App& a, const std::vector<Vec2>& trail);
