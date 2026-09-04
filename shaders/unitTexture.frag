#version 150

in vec2 fragTexCoord;
in float height;
in float shadeLevel;
out vec4 outColor;

uniform sampler2D textureSampler;
uniform float seaLevel;
uniform bool shade;
// How much of its own colour the model keeps where it covers the screen. 1 for
// everything except a cloaked unit, which the original averages with whatever
// is behind it. See RenderService::drawUnitMeshBatch.
uniform float alpha;

const vec3 waterTint = vec3(0.5, 0.5, 1.0);
const vec3 normalTint = vec3(1.0, 1.0, 1.0);
// TA lights its models after all. The renderer decoded earlier -- no normals,
// no sun, texels copied unmodified -- is the one that runs with SHADING
// switched OFF; the option defaults ON, and 0x458744 picks between two
// complete rasterizer chains on that one bit. The shaded chain averages the
// unit normals of the polygons meeting at a vertex and takes
//
//     level = (int)(5.0 * dot(n, (-0.8, 1.0, 0.25))) & 0x1F
//
// as a row of PALETTE.SHD. Three things about that line matter here. The mask
// WRAPS, and reproducing the wrap rather than clamping to the table's ends is
// what finally matched the original's contrast -- a screenshot comparison
// settled it against the earlier reading, which is why nothing below clamps
// to an end. The level is computed and masked per VERTEX, not per pixel, so
// unitTexture.vert does that arithmetic and only the resulting row arrives
// here interpolated. And the sun vector is not normalised: its length of
// 1.3048 sets the ramp's width at about thirteen rows rather than thirty-two.
//
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

void main(void)
{
    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    vec3 lit = vec3(baseColor) * shadeIntensity() * (height > seaLevel ? normalTint : waterTint);
    outColor = vec4(min(lit, vec3(1.0)), alpha);
}
