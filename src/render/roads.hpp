#pragma once
#include "core/camera.hpp"
#include "city/mapgen.hpp"

#include <glad/gl.h>
#include <string>

// Streets: one quad per street, one disc per junction; roads.frag paints the surface
// from each pixel's position along / across the street, so it works at any angle.
// Two passes (sidewalks, then asphalt) so no sidewalk covers a carriageway at junctions.
class RoadRenderer {
public:
    void init(const std::string& shaderDir);
    void upload(const CityMap& map);
    void draw(const Camera& cam, int fbW, int fbH) const;
    void destroy();

private:
    GLuint  prog_ = 0, vao_ = 0, vbo_ = 0;
    GLsizei count_ = 0;
};
