#include "render/heatmap.hpp"

#include "core/gl_util.hpp"

#include <algorithm>
#include <vector>

void HeatMap::init(const std::string& shaderDir) {
    prog_ = gl::linkProgram({gl::compileShader(GL_VERTEX_SHADER, shaderDir + "/heat.vert"),
                             gl::compileShader(GL_FRAGMENT_SHADER, shaderDir + "/heat.frag")});
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &buf_);
    glNamedBufferStorage(buf_, GLsizeiptr(kDim) * kDim * 4, nullptr, 0);
    clear();
}

void HeatMap::setCity(const CityMap& city) {
    size_ = city.size;
    // Land = cells with a street node; water and the palace park do not count towards the average.
    std::vector<uint8_t> land(size_t(kDim) * kDim, 0);
    for (const Vec2& p : city.nodes) {
        const uint32_t x = std::min(uint32_t(p.x / size_ * kDim), kDim - 1);
        const uint32_t y = std::min(uint32_t(p.y / size_ * kDim), kDim - 1);
        land[size_t(y) * kDim + x] = 1;
    }
    landCells_ = std::max<uint32_t>(1, uint32_t(std::count(land.begin(), land.end(), uint8_t(1))));
    clear();
}

void HeatMap::clear() const {
    const GLuint zero = 0;
    glClearNamedBufferData(buf_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
}

void HeatMap::bind() const { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 18, buf_); }

void HeatMap::draw(const Camera& cam, int fbW, int fbH, float average, float opacity) const {
    if (opacity <= 0.0f) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glProgramUniform2f(prog_, 0, cam.cx, cam.cy);
    glProgramUniform2f(prog_, 1, 2.0f * cam.ppm / float(fbW), 2.0f * cam.ppm / float(fbH));
    glProgramUniform1f(prog_, 2, size_);
    glProgramUniform1ui(prog_, 3, kDim);
    glProgramUniform1f(prog_, 4, average);
    glProgramUniform1f(prog_, 5, opacity);
    bind();
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
}

void HeatMap::destroy() {
    glDeleteBuffers(1, &buf_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(prog_);
}
