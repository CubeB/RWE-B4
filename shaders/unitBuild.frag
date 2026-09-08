#version 150

in vec2 fragTexCoord;
in float height;
in float shadeLevel;
out vec4 outColor;

uniform sampler2D textureSampler;
// The same atlas again, one byte a texel: that texel's raw palette index.
uniform sampler2D paletteIndexSampler;
// palettes/PALETTE.SHD as a 256x32 image; see unitTexture.frag.
uniform sampler2D shadeTableSampler;
uniform float unitY;
uniform float seaLevel;
uniform float shadeStrength;
// Height of the whole model above unitY.
uniform float unitHeight;

// The construction display, as the original draws it. It keeps an 8-bit
// height per pixel — the model-space Y of the topmost surface there, plus 50 —
// and remaps every pixel by comparing that against a moving threshold. Above
// the line, in a four-unit band on it, and below it are each drawn one of
// four ways, and which three ways depends on how far the build has got.
uniform float buildRatio;
// 0 = not drawn, 1 = build colour A, 2 = build colour B, 3 = the texture.
uniform int aboveMode;
uniform int bandMode;
uniform int belowMode;
// Two triangle waves over palette entries 160..175 at different rates.
uniform vec3 buildColorA;
uniform vec3 buildColorB;

// The band on the line is four of the original's height units tall.
const float bandThickness = 4.0;
// The original's height buffer is the model-space Y biased by 50.
const float heightBias = 50.0;

const vec3 waterTint = vec3(0.5, 0.5, 1.0);
const vec3 normalTint = vec3(1.0, 1.0, 1.0);

// The shaded chain's lighting; see unitTexture.frag, which this must match.
// The level is computed once per vertex and interpolated, and the row it
// yields indexes PALETTE.SHD -- which stores palette INDICES from a
// nearest-neighbour search, not a scale, so it has to be read as a table and
// not approximated by a brightness curve. The texel's own palette index rides
// through the atlas in a second single-channel copy to make that possible.
vec3 shadeTexel(vec3 unshaded)
{
    if (shadeStrength <= 0.0)
    {
        return unshaded;
    }

    // texel = the raw palette index; row = shade >> 16, truncated; the pixel
    // is SHD[row * 256 + texel] and nothing else (0x4C81A4-0x4C81BD). The
    // clamp is against the array bound alone -- both ends of the
    // interpolation are already masked rows in 0..31.
    int row = clamp(int(shadeLevel), 0, 31);
    int texel = int((texture(paletteIndexSampler, fragTexCoord).r * 255.0) + 0.5);
    vec3 tableColor = texelFetch(shadeTableSampler, ivec2(texel, row), 0).rgb;

    return mix(unshaded, tableColor, shadeStrength);
}

vec3 shadeNormal()
{
    vec3 baseColor = vec3(texture(textureSampler, fragTexCoord));
    return min(shadeTexel(baseColor) * (height > seaLevel ? normalTint : waterTint), vec3(1.0));
}

void main(void)
{
    // Where this fragment sits in the original's height buffer. It keeps the
    // topmost surface per pixel; shading each fragment by its own height is
    // the same thing wherever the model does not overhang itself, and is
    // better behaved where it does.
    float z = (height - unitY) + heightBias;

    int mode;
    if (z >= buildRatio)
    {
        mode = aboveMode;
    }
    else if (z >= buildRatio - bandThickness)
    {
        mode = bandMode;
    }
    else
    {
        mode = belowMode;
    }

    if (mode == 0)
    {
        // Erased: this part of the model has not been laid down yet.
        outColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
    else if (mode == 1)
    {
        outColor = vec4(buildColorA, 1.0);
    }
    else if (mode == 2)
    {
        outColor = vec4(buildColorB, 1.0);
    }
    else
    {
        outColor = vec4(shadeNormal(), 1.0);
    }
}
