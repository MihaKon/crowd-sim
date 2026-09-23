#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"

#include <glad/gl.h>
#include <string>
#include <vector>

// Buildings in oblique 2.5D with procedural facades and ground shadows.
// One 32-byte record per building; the vertex shaders expand it from gl_VertexID.
// Records are stored north to south, so painter's order puts nearer buildings on top.
class BuildingRenderer {
public:
    void   init(const std::string& shaderDir);
    void   upload(const CityMap& map);
    void   drawShadows(const Camera& cam, int fbW, int fbH) const;
    void   draw(const Camera& cam, int fbW, int fbH, float night) const;
    void   destroy();
    size_t buildingCount() const { return count_; }

private:
    struct Range {
        GLint   firstBuilding;
        GLsizei buildings;
        float   minX, minY, maxX, maxY;
    };
    bool collect(const Camera& cam, int fbW, int fbH, int vertsPerBuilding) const;

    GLuint                       prog_ = 0, shadowProg_ = 0, vao_ = 0, ssbo_ = 0;
    size_t                       count_ = 0;
    std::vector<Range>           ranges_;
    mutable std::vector<GLint>   firsts_;
    mutable std::vector<GLsizei> counts_;
};
