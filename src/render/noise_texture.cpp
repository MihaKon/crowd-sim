#include "render/noise_texture.hpp"

#include <cstdint>
#include <vector>

namespace {

constexpr int kSize = 256;

uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// Value noise on a lattice of `cell` texels that wraps at kSize.
float valueNoise(int x, int y, int cell, uint32_t seed) {
    const int   n  = kSize / cell;
    const int   ix = x / cell, iy = y / cell;
    float       tx = float(x % cell) / float(cell), ty = float(y % cell) / float(cell);
    tx             = tx * tx * (3.0f - 2.0f * tx);
    ty             = ty * ty * (3.0f - 2.0f * ty);
    auto v = [&](int cx, int cy) { return float(hash(uint32_t(cx % n), uint32_t(cy % n), seed) >> 8) / 16777216.0f; };
    const float a = v(ix, iy), b = v(ix + 1, iy), c = v(ix, iy + 1), d = v(ix + 1, iy + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

float fbm(int x, int y, int cell, uint32_t seed) {
    float s = 0.0f, a = 0.5f, norm = 0.0f;
    for (int o = 0; o < 3 && cell >= 2; ++o, cell /= 2, a *= 0.5f) {
        s += a * valueNoise(x, y, cell, seed + uint32_t(o) * 101u);
        norm += a;
    }
    return s / norm;
}

uint8_t byte(float v) { return uint8_t(v <= 0.0f ? 0 : v >= 1.0f ? 255 : int(v * 255.0f + 0.5f)); }

} // namespace

void NoiseTexture::init() {
    std::vector<uint8_t> px(size_t(kSize) * kSize * 4);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            uint8_t* p = &px[(size_t(y) * kSize + size_t(x)) * 4];
            p[0]       = byte(valueNoise(x, y, 8, 11u));
            p[1]       = byte(fbm(x, y, 16, 23u));
            p[2]       = byte(fbm(x, y, 16, 37u));
            p[3]       = uint8_t(hash(uint32_t(x), uint32_t(y), 53u) >> 24);
        }
    glCreateTextures(GL_TEXTURE_2D, 1, &tex_);
    glTextureStorage2D(tex_, 9, GL_RGBA8, kSize, kSize);
    glTextureSubImage2D(tex_, 0, 0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateTextureMipmap(tex_);
    glTextureParameteri(tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void NoiseTexture::destroy() {
    glDeleteTextures(1, &tex_);
    tex_ = 0;
}
