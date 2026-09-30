#pragma once
#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

// Immediate-mode UI: rounded rectangles (anti-aliased, with soft shadows) and
// monospace text in two sizes, batched into one draw call. Framebuffer pixels,
// y down, ASCII only.
class Ui {
public:
    enum Size { kSmall = 0, kLarge = 1 };

    void  init(const std::string& shaderDir, const std::string& fontPath, float pixelHeight);
    void  begin(int fbW, int fbH);
    void  rect(float x, float y, float w, float h, uint32_t rgb, float alpha = 1.0f);
    void  roundRect(float x, float y, float w, float h, float radius, uint32_t rgb, float alpha = 1.0f);
    void  shadow(float x, float y, float w, float h, float radius, float blur, float alpha);
    void  circle(float cx, float cy, float r, uint32_t rgb, float alpha = 1.0f);
    float text(float x, float y, const std::string& s, uint32_t rgb, float alpha = 1.0f, Size size = kSmall);
    float textWidth(const std::string& s, Size size = kSmall) const { return float(s.size()) * font_[size].advance; }
    float lineHeight(Size size = kSmall) const { return font_[size].lineHeight; }
    float charWidth(Size size = kSmall) const { return font_[size].advance; }
    void  end();
    void  destroy();

private:
    struct Vertex {
        float    x, y, u, v;
        uint32_t color;
        float    shape[4]; // rounded rect: half width, half height, radius, feather; 0: text / solid
    };
    struct Font {
        float                      ascent = 0.0f, lineHeight = 16.0f, advance = 8.0f;
        std::vector<unsigned char> packed;
    };
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color);
    void shapeQuad(float cx, float cy, float hw, float hh, float radius, float feather, uint32_t color);

    GLuint              prog_ = 0, vao_ = 0, vbo_ = 0, tex_ = 0;
    int                 fbW_ = 1, fbH_ = 1, atlasW_ = 512, atlasH_ = 512;
    Font                font_[2];
    std::vector<Vertex> verts_;
};
