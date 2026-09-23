#pragma once
#include "app/app.hpp"
#include "app/hud.hpp"
#include "city/mapgen.hpp"
#include "core/gpu_timer.hpp"
#include "render/building_render.hpp"
#include "render/heatmap.hpp"
#include "render/map_render.hpp"
#include "render/roads.hpp"
#include "render/sprite_atlas.hpp"
#include "sim/agents.hpp"
#include "sim/traffic.hpp"
#include "ui/inspector.hpp"
#include "ui/selection.hpp"
#include "ui/ui.hpp"

#include <chrono>
#include <vector>

// Owns every subsystem and runs the frame loop: simulate, pick, inspect, draw, log.
class Runner {
public:
    Runner(GLFWwindow* win, App& app, GLuint agentCount);
    ~Runner();
    Runner(const Runner&)            = delete;
    Runner& operator=(const Runner&) = delete;

    void run();

private:
    using Clock = std::chrono::steady_clock;

    struct StepPlan {
        double frameMs = 0.0, stepMs = 0.0;
        int    steps      = 0;
        bool   carLimited = false;
    };
    struct Layers {
        float heat = 0.0f, congestion = 0.0f;
        bool  density = false, pointsHidden = false;
    };

    void     rebuildWorld();
    void     updateFollow(float dt);
    StepPlan planSteps(float dt) const;
    Layers   chooseLayers() const;
    void     simulate(const StepPlan& plan, const Layers& layers, bool statTick);
    void     pickRandomPerson();
    void     pickUnderCursor();
    void     refreshInspector(Clock::time_point now);
    void     render(const Layers& layers);
    void     logStats(Clock::time_point now);
    GLuint   lodStride(uint32_t outdoor, GLuint count, double budgetScale = 1.0) const;

    GLFWwindow* win_;
    App&        a_;
    GLuint      count_;

    CityMap          city_;
    MapRenderer      map_;
    RoadRenderer     roads_;
    BuildingRenderer buildings_;
    Agents           agents_;
    Traffic          traffic_;
    Selection        selection_;
    HeatMap          heat_;
    SpriteAtlas      sprites_;
    Ui               ui_;
    GpuTimer         mapTimer_, buildTimer_, drawTimer_;

    double            simMs_ = 0.0;
    double            passMs_[2] = {};
    double            trafficMs_ = 0.0;
    bool              measureNext_ = false;
    bool              carLimited_ = false;
    Agents::Stats     stats_;
    HudInfo           hud_;
    Panel             panel_;
    uint32_t          markerCar_ = Selection::kNone;
    bool              refreshPanel_ = false;
    Clock::time_point panelTime_, statStart_;
    uint32_t          statFrames_ = 0;
    uint32_t          frameIndex_ = 0;
    bool              heatWasOn_ = false;
    uint32_t          picks_ = 0;
    std::vector<Vec2> trail_;
    uint32_t          trailOf_[2] = {Selection::kNone, Selection::kNone};
};
