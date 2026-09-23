#pragma once
#include <glad/gl.h>
#include <cstdint>
#include <string>

// People and roofs: Kenney "RPG Urban Pack" atlas (16 px tiles, 27 x 18).
// Cars: top-down pixel art generated in code (the pack only has side views).
class SpriteAtlas {
public:
    struct Uv {
        float x0, y0, x1, y1;
    };

    static constexpr int kCarW = 12, kCarH = 24, kCarVariants = 9;

    void init(const std::string& atlasPath);
    void bind(GLuint unit = 0) const {
        glBindTextureUnit(unit, tex_);
        glBindTextureUnit(unit + 1, carTex_);
    }
    void destroy();

    // skin 0-5, direction 0 down / 1 right / 2 up / 3 left, frame 0 idle / 1-2 walking
    Uv person(int skin, int direction, int frame) const;

private:
    Uv tile(int col, int row) const;
    void buildCars();
    GLuint tex_ = 0, carTex_ = 0;
    int    cols_ = 27, rows_ = 18;
};
