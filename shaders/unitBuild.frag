#version 150

in vec2 fragTexCoord;
in float height;
in float shadeLevel;
out vec4 outColor;

uniform sampler2D textureSampler;
uniform float unitY;
uniform float seaLevel;
uniform bool shade;
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
// PALETTE.SHD's row k remaps each texel to the nearest palette entry to
// `colour * 0.06875k`, and row 15 is the identity -- the constant the exe
// hard-codes for a piece the COB has told not to shade. The exact table is a
// nearest-neighbour remap in a 256-entry palette, which would need each
// texel's palette index carried through the atlas to reproduce faithfully;
// that is written up as still to do.
// The row is the original's, exactly -- see the probe in section 13 of
// TOTALA-EXE-SHADING.md, which this reproduces primitive for primitive. What
// a row MEANS is where RWE departs, in three measured steps.
//
// First, the table is not the linear `0.06875 * row` its generator suggests.
// PALETTE.SHD stores palette INDICES from a nearest-neighbour search, so the
// bright half runs out of palette to move to and saturates: measured over the
// real entries, row 16 lands at 1.07 and row 31 at only 1.55, not 2.13. The
// two-segment fit below tracks those measurements to within about 0.03 --
// linear below the identity row, and a much shallower slope above it, which
// is what stops lit faces blowing out.
//
// Second, the original truncates the interpolated row to an integer at every
// pixel, quantising each gradient into at most thirty-two bands. Leaving it
// continuous keeps the transition into shadow smooth.
//
// Third, its row 0 is pure black, and with the wrap a good deal of a model
// lands there -- measured against a screenshot of the original, RWE at row 0
// put 40% of a solar collector's pixels below luminance 8 where the original
// had 25%, with correspondingly fewer mid-tones. The floor keeps some light
// in a shadowed face. It is the one number here chosen by eye rather than
// measured, and it is the one to turn if the shadows want to be deeper.
const float shadowFloor = 0.25;
const float identityRow = 15.0;
const float darkSlope = 0.06875;
const float litSlope = 0.0325;

float shadeIntensity()
{
    if (!shade)
    {
        return 1.0;
    }

    float row = clamp(shadeLevel, 0.0, 31.0);
    float tableValue = row <= identityRow
        ? darkSlope * row
        : (darkSlope * identityRow) + ((row - identityRow) * litSlope);

    return shadowFloor + ((1.0 - shadowFloor) * tableValue);
}

vec3 shadeNormal()
{
    vec3 baseColor = vec3(texture(textureSampler, fragTexCoord));
    return min(baseColor * shadeIntensity() * (height > seaLevel ? normalTint : waterTint), vec3(1.0));
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
