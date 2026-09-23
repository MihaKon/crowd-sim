#include "app/runner.hpp"

#include "core/palette.hpp"

#include <cmath>
#include <cstdio>

namespace {

// LOD budget: drawn points cover roughly this fraction of the screen.
constexpr double kCoverage = 1.0;

double elapsedMs(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

float channel(uint32_t hex, int shift) { return float((hex >> shift) & 0xFFu) / 255.0f; }

// 0 by day, 1 by night; dusk 18:00-19:30, dawn 04:30-06:00.
float nightFactor(double simMs) {
    const float m = float((uint64_t(simMs) + Agents::kDayOffset) % 86'400'000) / 60000.0f;
    return m >= 1080.0f ? std::min((m - 1080.0f) / 90.0f, 1.0f)
         : m < 270.0f   ? 1.0f
         : m < 360.0f   ? 1.0f - (m - 270.0f) / 90.0f
                        : 0.0f;
}

} // namespace

Runner::Runner(GLFWwindow* win, App& app, GLuint agentCount) : win_(win), a_(app), count_(agentCount) {
    const std::string dir = SHADER_DIR;
    map_.init(dir);
    roads_.init(dir);
    buildings_.init(dir);
    agents_.init(dir);
    traffic_.init(dir);
    selection_.init(dir);
    heat_.init(dir);
    sprites_.init(SPRITE_ATLAS_PATH);
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(win_, &sx, &sy);
    ui_.init(dir, FONT_PATH, std::round(15.0f * sx));

    rebuildWorld();

    glEnable(GL_PROGRAM_POINT_SIZE);
    mapTimer_.init();
    buildTimer_.init();
    drawTimer_.init();
}

Runner::~Runner() {
    mapTimer_.destroy();
    buildTimer_.destroy();
    drawTimer_.destroy();
    ui_.destroy();
    buildings_.destroy();
    roads_.destroy();
    sprites_.destroy();
    heat_.destroy();
    selection_.destroy();
    traffic_.destroy();
    agents_.destroy();
    map_.destroy();
}

void Runner::rebuildWorld() {
    auto t0     = Clock::now();
    city_       = generateCity(a_.seed, kWorldSize);
    const double genMs = elapsedMs(t0);

    t0 = Clock::now();
    map_.upload(city_);
    roads_.upload(city_);
    buildings_.upload(city_);
    agents_.setWorld(city_);
    agents_.reset(count_, a_.seed);
    const double upMs = elapsedMs(t0);

    t0 = Clock::now();
    traffic_.setWorld(city_, agents_.world(), agents_.worldBuffer());
    traffic_.reset(count_, a_.seed, agents_.statusBuffer(), agents_.statsBuffer());
    agents_.setCarBuffers(traffic_.carLaneBuffer(), traffic_.carRouteBuffer());
    agents_.setCarOwnership(traffic_.ownerEvery());
    const double trMs = elapsedMs(t0);
    heat_.setCity(city_);

    const SimWorld&     w  = agents_.world();
    const TrafficWorld& tw = traffic_.world();
    size_t              kinds[5] = {};
    for (const District& d : city_.districts) ++kinds[int(d.kind)];
    std::printf("\ncity seed %u: generated in %.0f ms, tables + upload %.0f ms\n"
                "  %zu nodes, %zu streets, %zu blocks, %u stations on %u lines, sim tables %.1f MB\n"
                "  trains: %zu slots, %u trips/day, %u passengers per train\n"
                "  districts: %zu (business %zu, residential %zu, wealthy %zu, poor %zu, industrial %zu)\n"
                "  job slots by pay: low %u, mid %u, high %u\n"
                "  buildings: %zu\n"
                "  traffic: %u lanes (%u car slots), %u signals, %u routing regions, tables %.0f ms\n"
                "  cars: %u private (every %u. agent), %u vans/taxis\n",
                a_.seed, genMs, upMs, city_.nodes.size(), city_.edges.size(), city_.blocks.size(), w.stationCount,
                w.lineCount, double(w.data.size()) * 4.0 / (1 << 20), w.trainSlots.size() / 2, w.occupancyCount,
                agents_.trainCapacity(), city_.districts.size(), kinds[0], kinds[1], kinds[2], kinds[3], kinds[4],
                w.workByPay[0], w.workByPay[1], w.workByPay[2], city_.buildings.size(), tw.laneCount, tw.queueSize,
                tw.signalCount, tw.regionCount, trMs, traffic_.ownerCount(), traffic_.ownerEvery(),
                traffic_.fleetCount());
}

void Runner::run() {
    auto prev  = Clock::now();
    statStart_ = panelTime_ = prev;

    while (!glfwWindowShouldClose(win_)) {
        glfwPollEvents();
        if (a_.fbW == 0 || a_.fbH == 0) {
            glfwWaitEvents();
            prev = Clock::now();
            continue;
        }
        if (a_.regenerate) {
            a_.regenerate = false;
            rebuildWorld();
            a_.selAgent = a_.selCar = Selection::kNone;
            simMs_ = 0.0;
            stats_ = {};
            prev   = Clock::now();
        }

        const auto  now = Clock::now();
        const float dt  = std::min(std::chrono::duration<float>(now - prev).count(), 0.05f);
        prev            = now;

        updateFollow(dt);
        const bool     statTick = std::chrono::duration<float>(now - statStart_).count() >= 1.0f;
        const StepPlan plan     = planSteps(dt);
        const Layers   layers   = chooseLayers();
        simulate(plan, layers, statTick);

        if (a_.randomPick) pickRandomPerson();
        if (a_.clickPending) pickUnderCursor();
        refreshInspector(now);

        render(layers);
        glfwSwapBuffers(win_);

        ++statFrames_;
        if (statTick) logStats(now);
    }
}

void Runner::updateFollow(float dt) {
    if (a_.selAgent != trailOf_[0] || a_.selCar != trailOf_[1]) {
        trail_.clear();
        trailOf_[0] = a_.selAgent;
        trailOf_[1] = a_.selCar;
    }
    if (!a_.inspecting()) a_.follow = false;
    const Selection::Tracked tracked = a_.inspecting() ? selection_.latest() : Selection::Tracked{};
    if (tracked.valid) {
        if (trail_.empty() || length(trail_.back() - tracked.pos) > 6.0f) trail_.push_back(tracked.pos);
        if (trail_.size() > 3000) trail_.erase(trail_.begin(), trail_.begin() + 500);
        if (a_.follow) {
            const float k = 1.0f - std::exp(-dt * 8.0f);
            a_.cam.cx += (tracked.pos.x - a_.cam.cx) * k;
            a_.cam.cy += (tracked.pos.y - a_.cam.cy) * k;
        }
    }
    if (a_.zoomTarget > 0.0f) { // exponential approach in log space: same speed for every zoom level
        const float k = 1.0f - std::exp(-dt * 3.0f);
        a_.cam.ppm    = std::exp(std::log(a_.cam.ppm) + (std::log(a_.zoomTarget) - std::log(a_.cam.ppm)) * k);
        if (std::abs(a_.cam.ppm / a_.zoomTarget - 1.0f) < 0.01f) {
            a_.cam.ppm    = a_.zoomTarget;
            a_.zoomTarget = 0.0f;
        }
    }
}

// Traffic runs in fixed steps of kStepMs, at most kMaxSteps per frame. At high
// speeds the step grows up to kMaxStepMs; beyond that the clock slows down.
Runner::StepPlan Runner::planSteps(float dt) const {
    StepPlan p;
    p.frameMs = a_.paused ? 0.0 : double(dt) * a_.timeScale * 1000.0;
    p.steps   = p.frameMs > 0.0 ? int(std::ceil(p.frameMs / Traffic::kStepMs)) : 0;
    if (p.steps > Traffic::kMaxSteps) {
        p.steps  = Traffic::kMaxSteps;
        p.stepMs = p.frameMs / p.steps;
        if (p.stepMs > Traffic::kMaxStepMs) {
            p.stepMs     = Traffic::kMaxStepMs;
            p.frameMs    = p.stepMs * p.steps;
            p.carLimited = true;
        }
    } else if (p.steps > 0) {
        p.stepMs = p.frameMs / p.steps;
    }
    return p;
}

// At city scale people and cars are noise: they are hidden halfway through the
// zoom-out and the active layer fades in around that point, so they never overlap.
Runner::Layers Runner::chooseLayers() const {
    const float fullPpm   = float(std::min(a_.fbW, a_.fbH)) / kWorldSize;
    const float zoomedOut = 1.0f - std::clamp((a_.cam.ppm - 1.6f * fullPpm) / (3.4f * fullPpm), 0.0f, 1.0f);
    const float fade      = std::clamp((zoomedOut - 0.35f) / 0.3f, 0.0f, 1.0f);
    Layers      l;
    l.density      = a_.heatMode == HeatMap::kOutdoors || a_.heatMode == HeatMap::kEveryone;
    l.heat         = l.density ? fade : 0.0f;
    l.congestion   = a_.heatMode == HeatMap::kTraffic ? fade : 0.0f;
    l.pointsHidden = zoomedOut >= 0.5f;
    return l;
}

// Power-of-two stride: zooming in only adds points (nested subsets, no flicker).
GLuint Runner::lodStride(uint32_t outdoor, GLuint count, double budgetScale) const {
    if (!a_.lod) return 1;
    const float  hw = 0.5f * float(a_.fbW) / a_.cam.ppm;
    const float  hh = 0.5f * float(a_.fbH) / a_.cam.ppm;
    const float  w = std::max(0.0f, std::min(a_.cam.cx + hw, kWorldSize) - std::max(a_.cam.cx - hw, 0.0f));
    const float  h = std::max(0.0f, std::min(a_.cam.cy + hh, kWorldSize) - std::max(a_.cam.cy - hh, 0.0f));
    const double visible = double(outdoor) * double(w) * double(h) / double(kWorldSize * kWorldSize);
    const double budget  = budgetScale * kCoverage * double(a_.fbW) * double(a_.fbH) / double(a_.pointSize * a_.pointSize);
    GLuint       s       = 1;
    while (visible / s > budget && s < count) s <<= 1;
    return s;
}

void Runner::simulate(const StepPlan& plan, const Layers& layers, bool statTick) {
    carLimited_ = plan.carLimited;
    traffic_.setLaneStats(layers.congestion > 0.0f);
    const bool heatUpdate = layers.heat > 0.0f && (frameIndex_++ % 3 == 0 || !heatWasOn_);
    heatWasOn_            = layers.heat > 0.0f;
    if (heatUpdate) heat_.clear();
    heat_.bind();
    agents_.setHeat(heatUpdate && layers.density ? a_.heatMode : 0u, HeatMap::kDim, heat_.cellsPerMetre());

    const GLuint carStride =
        layers.pointsHidden ? 0xFFFFFFFFu : lodStride(stats_.carsOnRoad, traffic_.carCount(), 0.25);
    if (!a_.inspecting()) markerCar_ = Selection::kNone;
    selection_.beginFrame();
    agents_.setSelected(a_.selAgent);
    traffic_.setSelectedCar(markerCar_);

    // GL timer queries around compute work read ~0 on some drivers (Mesa/Intel),
    // so once per log line the passes are timed on the CPU between glFinish calls,
    // one frame after the stats were counted so the counting doesn't inflate them.
    if (measureNext_) glFinish();
    const auto tTraffic = Clock::now();
    if (plan.steps == 0)
        traffic_.step(uint32_t(simMs_), 0.0f, false, true, statTick, a_.cam, a_.fbW, a_.fbH, carStride);
    for (int i = 0; i < plan.steps; ++i) {
        const bool last = i + 1 == plan.steps;
        traffic_.step(uint32_t(simMs_ + plan.stepMs * (i + 1)), float(plan.stepMs / 1000.0), true, last,
                      last && statTick, a_.cam, a_.fbW, a_.fbH, carStride);
    }
    if (measureNext_) {
        glFinish();
        trafficMs_ = elapsedMs(tTraffic);
    }
    simMs_ += plan.frameMs;

    const GLuint stride = layers.pointsHidden ? 0xFFFFFFFFu : lodStride(stats_.outdoor(), count_);
    agents_.update(uint32_t(simMs_), !a_.paused, a_.cam, a_.fbW, a_.fbH, stride, statTick,
                   measureNext_ ? passMs_ : nullptr);
    measureNext_ = statTick;
    selection_.endFrame();
}

void Runner::pickRandomPerson() {
    a_.randomPick = false;
    const uint32_t seed = (++picks_) * 747796405u ^ uint32_t(simMs_) ^ 0x9E3779B9u;
    uint32_t       id   = selection_.randomVisible(agents_.visibleBuffer(), agents_.visibleCapacity(), seed);
    for (uint32_t k = 0; id == Selection::kNone && k < 256; ++k) { // nobody drawn: find someone outdoors
        const uint32_t cand = ((seed + k) * 2654435761u) % count_;
        const uint32_t kind = agentLegKind(agents_.readAgent(cand).state);
        if (kind == 1 || kind == 2) id = cand;
    }
    if (id == Selection::kNone) id = (seed * 2654435761u) % count_;
    a_.selAgent   = id;
    a_.selCar     = Selection::kNone;
    a_.follow     = true;
    a_.zoomTarget = std::max(a_.cam.ppm, 1.2f);
    refreshPanel_ = true;
}

void Runner::pickUnderCursor() {
    a_.clickPending    = false;
    const float r      = pixelRatio(win_);
    const float sx     = float(a_.clickX) * r, sy = float(a_.clickY) * r;
    const bool onPanel = a_.showUi && a_.inspecting() && sx >= a_.panel[0] && sx <= a_.panel[2] &&
                         sy >= a_.panel[1] && sy <= a_.panel[3];
    if (onPanel) return;

    const Vec2 wp{a_.cam.cx + (sx - 0.5f * float(a_.fbW)) / a_.cam.ppm,
                  a_.cam.cy - (sy - 0.5f * float(a_.fbH)) / a_.cam.ppm};
    const Selection::Hit hit =
        selection_.pick(agents_.visibleBuffer(), agents_.visibleCapacity(), traffic_.visibleBuffer(),
                        traffic_.visibleCapacity(), wp, std::max(12.0f * r, 2.0f * a_.pointSize) / a_.cam.ppm);
    a_.selAgent = a_.selCar = Selection::kNone;
    if (hit.agent != Selection::kNone && hit.agentDist <= hit.carDist) {
        a_.selAgent = hit.agent;
    } else if (hit.car != Selection::kNone) {
        const CarRaw cr = traffic_.readCar(hit.car);
        if (cr.route[2] != Selection::kNone) a_.selAgent = cr.route[2]; // private car: inspect its driver
        else a_.selCar = hit.car;
    }
    refreshPanel_ = true;
}

void Runner::refreshInspector(Clock::time_point now) {
    if (!a_.inspecting() || (!refreshPanel_ && std::chrono::duration<float>(now - panelTime_).count() < 0.25f))
        return;
    refreshPanel_ = false;
    panelTime_    = now;
    const InspectContext ctx{city_, agents_.world(), a_.seed, Agents::kDayOffset, uint32_t(simMs_),
                             traffic_.ownerEvery(), traffic_.ownerCount()};
    markerCar_ = Selection::kNone;
    if (a_.selAgent != Selection::kNone) {
        const AgentRaw ar = agents_.readAgent(a_.selAgent);
        CarRaw         cr;
        const bool     inCar = agentLegKind(ar.state) == kAgentInCar;
        if (inCar) {
            cr         = traffic_.readCar(ar.leg[0]);
            markerCar_ = ar.leg[0];
        }
        panel_ = describeAgent(ctx, ar, inCar ? &cr : nullptr);
    } else {
        panel_     = describeCar(ctx, traffic_.readCar(a_.selCar));
        markerCar_ = a_.selCar;
    }
}

// Draw order: ground, streets, rail, shadows; cars and people; buildings over
// them (tall ones hide the street behind); overlays; marker; UI.
void Runner::render(const Layers& layers) {
    glClearColor(channel(palette::kBackground, 16), channel(palette::kBackground, 8),
                 channel(palette::kBackground, 0), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (a_.showMap) {
        mapTimer_.begin();
        map_.draw(a_.cam, a_.fbW, a_.fbH, MapRenderer::kGround);
        roads_.draw(a_.cam, a_.fbW, a_.fbH);
        map_.draw(a_.cam, a_.fbW, a_.fbH, MapRenderer::kOverlay);
        buildings_.drawShadows(a_.cam, a_.fbW, a_.fbH);
        mapTimer_.end();
    }
    if (a_.showAgents) {
        drawTimer_.begin();
        sprites_.bind(0);
        traffic_.draw(a_.cam, a_.fbW, a_.fbH);
        agents_.draw(a_.cam, a_.fbW, a_.fbH, a_.pointSize);
        drawTimer_.end();
    }
    if (a_.showMap) {
        buildTimer_.begin();
        sprites_.bind(0);
        buildings_.draw(a_.cam, a_.fbW, a_.fbH, nightFactor(simMs_));
        buildTimer_.end();
    }
    if (a_.showAgents) {
        drawTimer_.begin();
        const float counted = float(a_.heatMode == HeatMap::kEveryone ? count_ : stats_.outdoor());
        heat_.draw(a_.cam, a_.fbW, a_.fbH, counted / float(heat_.landCells()), layers.heat);
        traffic_.drawCongestion(a_.cam, a_.fbW, a_.fbH, layers.congestion);
        agents_.drawTrains(a_.cam, a_.fbW, a_.fbH, uint32_t(simMs_));
        drawTimer_.end();
    }
    if (a_.inspecting()) selection_.drawMarker(a_.cam, a_.fbW, a_.fbH, 18.0f * pixelRatio(win_));
    if (a_.showUi) {
        ui_.begin(a_.fbW, a_.fbH);
        if (a_.inspecting()) drawTrail(ui_, a_, trail_);
        hud_.simMs        = simMs_;
        hud_.carLimited   = carLimited_;
        hud_.layerOpacity = std::max(layers.heat, layers.congestion);
        hud_.people       = count_;
        hud_.stats        = stats_;
        const float top   = drawHud(ui_, a_, hud_);
        if (a_.inspecting()) drawPanel(ui_, a_, panel_, top);
        drawHelp(ui_, a_);
        ui_.end();
    }
}

void Runner::logStats(Clock::time_point now) {
    const float elapsed = std::chrono::duration<float>(now - statStart_).count();
    hud_.fps            = double(statFrames_) / elapsed;
    const uint32_t leftBefore = stats_.leftBehind;
    stats_                    = agents_.readStats();
    const uint32_t left       = stats_.leftBehind - leftBefore;
    const uint64_t tod        = (uint64_t(simMs_) + Agents::kDayOffset) / 60000 % 1440;
    std::printf("%02u:%02u %4.0f fps %5.2f ms | sim %5.2f cull %5.2f cars %5.2f map %5.2f draw %5.2f | "
                "walk %s wait %s train %s full %s | cars %s stopped %s | home %s work %s | x%g%s%s%s\n",
                unsigned(tod / 60), unsigned(tod % 60), double(statFrames_) / elapsed, 1000.0 * elapsed / statFrames_,
                passMs_[0], passMs_[1], trafficMs_, mapTimer_.takeAverage() + buildTimer_.takeAverage(),
                drawTimer_.takeAverage(), human(stats_.walking).c_str(), human(stats_.waiting).c_str(),
                human(stats_.riding).c_str(), human(left).c_str(), human(stats_.carsOnRoad).c_str(),
                human(stats_.carsStopped).c_str(), human(stats_.home).c_str(), human(stats_.work).c_str(),
                double(a_.timeScale), a_.paused ? " paused" : "", a_.lod ? "" : " LOD-off",
                carLimited_ ? " car-limited" : "");
    std::fflush(stdout);
    statStart_  = now;
    statFrames_ = 0;
}
