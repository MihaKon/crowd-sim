#pragma once
#include <glad/gl.h>

// A tileable 256 x 256 noise texture with mipmaps, bound to texture unit 7 for all
// world shaders (see frame.glsl). One filtered fetch replaces a dozen hashes per
// pixel, and the mipmaps keep procedural detail from shimmering when zoomed out.
//   R: value noise, lattice 8 texels     G: 3-octave fbm, lattice 16 texels
//   B: 3-octave fbm, another seed        A: white noise per texel
class NoiseTexture {
public:
    static constexpr GLuint kUnit = 7;
    void init();
    void bind() const { glBindTextureUnit(kUnit, tex_); }
    void destroy();

private:
    GLuint tex_ = 0;
};
