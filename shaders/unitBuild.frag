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
// hard-codes for a piece the COB has told not to shade. A plain multiply with
// a per-channel clamp reproduces that table to within about 5-7 of 255 over
// the real texel population of the stock unit textures; the clamp is not
// optional, since it is where all the bright-end behaviour comes from. The
// exact table is a nearest-neighbour remap in a 256-entry palette, which would
// need each texel's palette index carried through the atlas to reproduce
// faithfully; that is written up as still to do.
// Two deliberate departures from the original here, both asked for after
// looking at the two side by side, and both easy to put back.
//
// The original truncates the interpolated row to an integer per pixel, which
// quantises every gradient into at most thirty-two bands; dropping the floor
// keeps the interpolation continuous and the transition into shadow smooth.
//
// And row 0 is pure black, so a face that wraps to it goes to nothing at all.
// A shadow that keeps a little light in it reads better on a modern display
// and still leaves the lit end where the table puts it: the floor lifts the
// dark end and the scale is chosen so an unlit face (row 15, x1.031) comes
// out where it always did.
const float shadowFloor = 0.16;

float shadeIntensity()
{
    if (!shade)
    {
        return 1.0;
    }
    float tableValue = 0.06875 * clamp(shadeLevel, 0.0, 31.0);
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
