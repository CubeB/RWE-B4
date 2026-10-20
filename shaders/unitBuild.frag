#version 150

in vec2 fragTexCoord;
in float height;
in float shadeLevel;
out vec4 outColor;
// Coverage for the building halo; see unitTexture.frag and worldPost.frag.
out vec4 outMask;

uniform sampler2D textureSampler;
// The same atlas again, one byte a texel: that texel's raw palette index.
uniform sampler2D paletteIndexSampler;
// The shade table followed by its blue remap, 256x65; see unitTexture.frag
// and shadeAndBlueTablesToImage.
uniform sampler2D shadeTableSampler;
// 1 for a unit that is not the viewer's and not held on sonar: the parts of its
// model at or below the water line are cut away (0x4BA1B0) rather than tinted
// blue (BT-142). 0 tints them, as the viewer's own units and sonar contacts are.
uniform float waterCut;
// World height at or below which a fragment is cut away whatever the water is
// doing: a digger's origin plane (BT-143). Far below the world for everything
// that is not a digger.
uniform float cutBelowY;
uniform float unitY;
uniform float seaLevel;
uniform float shadeStrength;
// Height of the whole model above unitY.
uniform float unitHeight;

// The construction display, as the original draws it. It keeps an 8-bit
// height per pixel -- the model-space Y of the topmost surface there, plus the
// per-unit bias below -- and remaps every pixel by comparing that against a
// moving threshold. Above
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
// The palette index each fill colour is drawn with. The original recolours
// the nanoframe by writing one of those entries, then runs the water blue
// remap over the finished picture, so an underwater fill has to be looked up
// through the blue table rather than tinted from its RGB (BT-142).
uniform int buildColorAIndex;
uniform int buildColorBIndex;

// The band on the line is four of the original's height units tall.
const float bandThickness = 4.0;
// How far the original's height buffer lifts a vertex above the model origin:
// 50, or 125 for a digger, whose model reaches below that origin. The larger
// value keeps the height byte from wrapping (TOTALA-EXE-SHADING.md S:04,
// S:11, S:22; model-render parity rule 8).
uniform float heightBias;

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

// The part of a model at or below the water line goes through the original's
// blue table (BT-142, 0x4B96E0): red halved, green halved, blue halved plus 50,
// snapped back to a palette entry -- not a multiplier. The tint is applied to
// the finished, already shaded pixel, so the combined table image carries it
// ready-made: rows 32..63 are the shade rows with every cell passed through the
// blue table, and row 64 is the blue table alone for an unshaded texel.
vec3 blueTexel()
{
    int texel = int((texture(paletteIndexSampler, fragTexCoord).r * 255.0) + 0.5);
    vec3 blueUnshaded = texelFetch(shadeTableSampler, ivec2(texel, 64), 0).rgb;
    if (shadeStrength <= 0.0)
    {
        return blueUnshaded;
    }

    int row = clamp(int(shadeLevel), 0, 31);
    vec3 blueShaded = texelFetch(shadeTableSampler, ivec2(texel, 32 + row), 0).rgb;
    return mix(blueUnshaded, blueShaded, shadeStrength);
}

vec3 shadeNormal()
{
    vec3 baseColor = vec3(texture(textureSampler, fragTexCoord));
    return min(height > seaLevel ? shadeTexel(baseColor) : blueTexel(), vec3(1.0));
}

// A flat build fill is a palette entry, not a texture sample. The original
// recolours the picture to that index and only then applies the water pass to
// the whole finished picture, so below the sea line the entry goes through the
// blue table just as a texel does (BT-142; model-render parity rule 8). Row 64
// of the combined table is the blue table alone -- the same row blueTexel()
// reads for an unshaded texel -- so a fill pixel becomes the entry the
// original's blitter would produce.
vec3 buildFillColor(vec3 aboveWater, int paletteIndex)
{
    if (height > seaLevel)
    {
        return aboveWater;
    }
    return texelFetch(shadeTableSampler, ivec2(paletteIndex, 64), 0).rgb;
}

void main(void)
{
    // The cut applies to the whole finished picture, whichever way the build
    // display would draw this fragment.
    if (!(height > seaLevel) && waterCut > 0.5)
    {
        discard;
    }
    if (height <= cutBelowY)
    {
        discard;
    }

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
        // Erased: this part of the model has not been laid down yet. The
        // original writes the transparent key and the blitter skips those
        // pixels, so nothing of them reaches the frame -- not even depth.
        // Writing alpha 0 instead would still leave an occluder behind the
        // see-through parts (and would clear the halo mask of a finished
        // building already drawn behind them); a discard writes neither
        // colour, depth nor mask.
        discard;
    }
    else
    {
        if (mode == 1)
        {
            outColor = vec4(buildFillColor(buildColorA, buildColorAIndex), 1.0);
        }
        else if (mode == 2)
        {
            outColor = vec4(buildFillColor(buildColorB, buildColorBIndex), 1.0);
        }
        else
        {
            outColor = vec4(shadeNormal(), 1.0);
        }

        // An occluder, never a halo source: the original's halo comes out of a
        // FINISHED building's cached bitmap, and a thing under construction
        // has no cached bitmap yet.
        outMask = vec4(0.0, 0.0, 0.0, 0.5);
    }
}
