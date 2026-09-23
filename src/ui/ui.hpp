#pragma once
#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

// Immediate-mode UI: rectangles and monospace text, batched into one draw call.
// Framebuffer pixels, y down, ASCII only.
class Ui {
public:
    void  init(const std::string& shaderDir, const std::string& fontPath, float pixelHeight);
    void  begin(int fbW, int fbH);
    void  rect(float x, float y, float w, float h, uint32_t rgb, float alpha = 1.0f);
    float text(float x, float y, const std::string& s, uint32_t rgb, float alpha = 1.0f);
    float textWidth(const std::string& s) const { return float(s.size()) * advance_; }
    float lineHeight() const { return lineHeight_; }
    float charWidth() const { return advance_; }
    void  end();
    void  destroy();

private:
    struct Vertex {
        float    x, y, u, v;
        uint32_t color;
    };
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color);

    GLuint              prog_ = 0, vao_ = 0, vbo_ = 0, tex_ = 0;
    int                 fbW_ = 1, fbH_ = 1, atlasW_ = 512, atlasH_ = 512;
    float               ascent_ = 0.0f, lineHeight_ = 16.0f, advance_ = 8.0f;
    std::vector<Vertex> verts_;
    std::vector<unsigned char> packed_;
};
