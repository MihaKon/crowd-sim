#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"

#include <glad/gl.h>
#include <cstdint>
#include <string>

// City-wide density map: agents.comp atomically counts people per cell, heat.frag draws it.
class HeatMap {
public:
    static constexpr uint32_t kDim = 256;
    enum Mode : uint32_t { kOff = 0, kOutdoors = 1, kEveryone = 2, kTraffic = 3 };

    void init(const std::string& shaderDir);
    void setCity(const CityMap& city);
    void clear() const;
    void bind() const;
    void draw(const Camera& cam, int fbW, int fbH, float average, float opacity) const;
    void destroy();

    float    cellsPerMetre() const { return float(kDim) / size_; }
    uint32_t landCells() const { return landCells_; }

private:
    GLuint   prog_ = 0, vao_ = 0, buf_ = 0;
    float    size_ = 1.0f;
    uint32_t landCells_ = 1;
};
