#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"

#include <glad/gl.h>
#include <string>
#include <vector>

// Static geometry (countryside, water, shore, blocks, rail, stations), binned into layer x tile ranges;
// only ranges in view are drawn, in layer order, with one multi-draw.
class MapRenderer {
public:
    void   init(const std::string& shaderDir);
    void   upload(const CityMap& map);
    enum Layers { kGround = 0, kOverlay = 1 };
    void   draw(const Camera& cam, int fbW, int fbH, Layers which) const;
    void   destroy();
    size_t vertexCount() const { return total_; }

    struct Range {
        GLint   first;
        GLsizei count;
        float   minX, minY, maxX, maxY;
        int     layer;
    };

private:
    void drawLayers(const Camera& cam, int fbW, int fbH, int lo, int hi) const;

    GLuint                       prog_ = 0, vao_ = 0, vbo_ = 0;
    size_t                       total_ = 0;
    float                        size_  = 1.0f;
    GLuint                       fieldTex_ = 0;             // distance to the waterline, R16F metres
    float                        fieldOrigin_ = 0.0f, fieldExtent_ = 1.0f;
    std::vector<Range>           ranges_;
    mutable std::vector<GLint>   firsts_;
    mutable std::vector<GLsizei> counts_;
};
