#include "render/sprite_atlas.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {
constexpr int kTilePx = 16;
// Characters: 6 skins x 3 rows (idle, walk A, walk B). Columns 23-26 are profile left,
// front, back, profile right (verified pixel by pixel: 23/26 mirror, 24/25 symmetric).
constexpr int kFacingCol[4] = {24, 26, 25, 23};
constexpr int kCharRow0     = 0;

// Top-down cars, nose up, 12 x 24. Legend:
//   . empty   o outline   b body   h body highlight   g glass   y headlight
//   r tail light (flagged in alpha, lit by cars.frag when braking)
//   m mirror   s roof sign   w cargo box   k box seam
const char* kSedan[SpriteAtlas::kCarH] = {
    "............", "...oooooo...", "..obbbbbbo..", ".oybbbbbbyo.", ".obbbhhbbbo.", ".obbbhhbbbo.",
    ".obbbbbbbbo.", ".oggggggggo.", "moggggggggom", ".oggggggggo.", ".obbbbbbbbo.", ".obhhhhhhbo.",
    ".obbbbbbbbo.", ".obbbbbbbbo.", ".obbbbbbbbo.", ".oggggggggo.", ".oggggggggo.", ".obbbbbbbbo.",
    ".obbbbbbbbo.", ".orbbbbbbro.", "..obbbbbbo..", "...oooooo...", "............", "............"};
const char* kTaxi[SpriteAtlas::kCarH] = {
    "............", "...oooooo...", "..obbbbbbo..", ".oybbbbbbyo.", ".obbbhhbbbo.", ".obbbhhbbbo.",
    ".obbbbbbbbo.", ".oggggggggo.", "moggggggggom", ".oggggggggo.", ".obbbbbbbbo.", ".obbossobbo.",
    ".obbossobbo.", ".obbbbbbbbo.", ".obbbbbbbbo.", ".oggggggggo.", ".oggggggggo.", ".obbbbbbbbo.",
    ".obbbbbbbbo.", ".orbbbbbbro.", "..obbbbbbo..", "...oooooo...", "............", "............"};
const char* kVan[SpriteAtlas::kCarH] = {
    "...oooooo...", "..obbbbbbo..", ".oybbbbbbyo.", ".obbbhhbbbo.", ".oggggggggo.", "moggggggggom",
    ".obbbbbbbbo.", ".oooooooooo.", ".owwwwwwwwo.", ".owwwwwwwwo.", ".owwwwwwwwo.", ".owkkkkkkwo.",
    ".owwwwwwwwo.", ".owwwwwwwwo.", ".owwwwwwwwo.", ".owkkkkkkwo.", ".owwwwwwwwo.", ".owwwwwwwwo.",
    ".owwwwwwwwo.", ".owwwwwwwwo.", ".orwwwwwwro.", "..oooooooo..", "............", "............"};

uint32_t rgba(uint32_t rgb, uint32_t a = 255) {
    return ((rgb >> 16) & 0xFFu) | (rgb & 0xFF00u) | ((rgb & 0xFFu) << 16) | (a << 24);
}
uint32_t shade(uint32_t rgb, float k) { // k < 1 darker, > 1 lighter
    auto ch = [&](int s) {
        const float c = float((rgb >> s) & 0xFFu);
        return uint32_t(std::clamp(k < 1.0f ? c * k : c + (255.0f - c) * (k - 1.0f), 0.0f, 255.0f)) << s;
    };
    return ch(16) | ch(8) | ch(0);
}
constexpr uint32_t kTailAlpha = 250; // marks tail-light pixels for cars.frag
} // namespace

void SpriteAtlas::init(const std::string& atlasPath) {
    int w, h, channels;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(atlasPath.c_str(), &w, &h, &channels, 4);
    if (!pixels) throw std::runtime_error("cannot load sprite atlas: " + atlasPath);
    cols_ = w / kTilePx;
    rows_ = h / kTilePx;

    // NEAREST when magnified keeps pixel art crisp; mipmaps when minified stop small or
    // rotated sprites from dropping rows. 16 px tiles: levels 1-4 never mix neighbours.
    glCreateTextures(GL_TEXTURE_2D, 1, &tex_);
    glTextureStorage2D(tex_, 5, GL_RGBA8, w, h);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(tex_, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    glGenerateTextureMipmap(tex_);
    glTextureParameteri(tex_, GL_TEXTURE_MAX_LEVEL, 4);
    glTextureParameteri(tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    buildCars();
}

// Variants, must match cars.vert: 0-5 private cars, 6 taxi, 7-8 delivery vans.
void SpriteAtlas::buildCars() {
    struct Variant {
        const char** art;
        uint32_t     body, box;
    };
    const Variant v[kCarVariants] = {
        {kSedan, 0xe8e8ec, 0}, {kSedan, 0xa9adb8, 0}, {kSedan, 0x34353f, 0}, {kSedan, 0xc64545, 0},
        {kSedan, 0x3d6fb6, 0}, {kSedan, 0x3f8f5f, 0}, {kTaxi, 0xe89a2c, 0},  {kVan, 0x3f8f5f, 0xeeeef0},
        {kVan, 0x3d6fb6, 0xeeeef0}};
    const int W = kCarW * kCarVariants, H = kCarH;
    std::vector<uint32_t> px(size_t(W) * H, 0);
    for (int k = 0; k < kCarVariants; ++k) {
        const uint32_t body    = v[k].body;
        const uint32_t outline = body == 0x34353f ? 0x121218 : shade(body, 0.45f);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < kCarW; ++x) {
                uint32_t c = 0;
                switch (v[k].art[y][x]) {
                case 'o': case 'm': c = rgba(outline); break;
                case 'b': c = rgba(body); break;
                case 'h': c = rgba(shade(body, 1.35f)); break;
                case 'g': c = rgba(0x2b3a55); break;
                case 'y': c = rgba(0xfff1b0); break;
                case 'r': c = rgba(0x8a2a2a, kTailAlpha); break;
                case 's': c = rgba(0xfafafa); break;
                case 'w': c = rgba(v[k].box); break;
                case 'k': c = rgba(shade(v[k].box, 0.8f)); break;
                default: break;
                }
                px[size_t(y) * W + size_t(k * kCarW + x)] = c;
            }
    }
    // 12 px wide sprites stay separate up to mip level 2.
    glCreateTextures(GL_TEXTURE_2D, 1, &carTex_);
    glTextureStorage2D(carTex_, 3, GL_RGBA8, W, H);
    glTextureSubImage2D(carTex_, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateTextureMipmap(carTex_);
    glTextureParameteri(carTex_, GL_TEXTURE_MAX_LEVEL, 2);
    glTextureParameteri(carTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(carTex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(carTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(carTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

SpriteAtlas::Uv SpriteAtlas::tile(int col, int row) const {
    const float x0 = float(col) / float(cols_), y0 = float(row) / float(rows_);
    return {x0, y0, x0 + 1.0f / float(cols_), y0 + 1.0f / float(rows_)};
}

SpriteAtlas::Uv SpriteAtlas::person(int skin, int direction, int frame) const {
    skin      = std::clamp(skin, 0, 5);
    frame     = std::clamp(frame, 0, 2);
    direction = std::clamp(direction, 0, 3);
    return tile(kFacingCol[direction], kCharRow0 + skin * 3 + frame);
}

void SpriteAtlas::destroy() {
    glDeleteTextures(1, &tex_);
    glDeleteTextures(1, &carTex_);
    tex_ = carTex_ = 0;
}
