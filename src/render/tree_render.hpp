#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"

#include <glad/gl.h>
#include <string>
#include <vector>

// Tree crowns as sun-shaded domes with a soft ground shadow along the sun. One
// 16-byte record per tree, expanded to a quad in the vertex shader; stored north to
// south like the buildings, so nearer crowns overlap farther ones.
class TreeRenderer {
public:
    void init(const std::string& shaderDir);
    void upload(const CityMap& map);
    void drawShadows(const Camera& cam, int fbW, int fbH, float strength) const; // strength 0..1
    void draw(const Camera& cam, int fbW, int fbH) const;
    void destroy();

private:
    struct Range {
        GLint   first;
        GLsizei count;
        float   minX, minY, maxX, maxY;
    };
    bool collect(const Camera& cam, int fbW, int fbH) const;

    GLuint                       prog_ = 0, shadowProg_ = 0, vao_ = 0, ssbo_ = 0;
    std::vector<Range>           ranges_;
    mutable std::vector<GLint>   firsts_;
    mutable std::vector<GLsizei> counts_;
};
