#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"

#include <glad/gl.h>
#include <string>
#include <vector>

// Static geometry (water, blocks, rail, stations), binned into layer x tile ranges;
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
    GLuint                       prog_ = 0, vao_ = 0, vbo_ = 0;
    size_t                       total_ = 0;
    std::vector<Range>           ranges_;
    mutable std::vector<GLint>   firsts_;
    mutable std::vector<GLsizei> counts_;
};
