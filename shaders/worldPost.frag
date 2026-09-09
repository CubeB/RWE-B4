#version 150

in vec2 fragTexCoord;
out vec4 outColor;

uniform sampler2D screenTexture;
uniform sampler2D dodgeMask;
// The buildings' own pixels as PALETTE INDICES, drawn at the supersampled
// size: red is the index, alpha is 1 where a building covered the sample and
// 0 where the buffer was cleared. See unitMask.frag, and the halo below for
// why it carries indices rather than coverage.
uniform sampler2D buildingMask;
// palettes/PALETTE.ALP as a 256x256 image: rgb is the colour of the blend of
// the two entries, and alpha is that blend's own palette index, so a result
// can be fed straight back in as the operand of another lookup. See
// AlphaTable.h.
uniform sampler2D alphaTable;
// The options screen's gamma. The original is not a gamma curve at all: it
// rewrites the palette as min(255, c * m) with m = 0.5 + v/24 over a slider
// of 20 steps, so it runs 0.5x to 1.333x and 1.0 is untouched. A straight
// multiply is therefore the faithful thing, and this blit is the one place
// the whole world view passes through.
uniform float gamma;

// The purple halo on buildings, reproduced by running the original's own
// arithmetic rather than by painting on an impression of the result.
//
// It is a bug, and it is the original's. Jon Mavor, who wrote the engine:
// "Ever notice that a lot of the buildings have a weird purple halo? Basically
// the table broke when dealing with the edge and transparency because I didn't
// have a correct way to represent that." He anti-aliased the non-animating
// part of a building by rendering it into a buffer of twice the size in each
// dimension and filtering it down through a lookup table -- "a lookup on the
// top two pixel and the bottom two pixels. The results from those two ops were
// then looked up to give me the final color, so 3 lookups."
//
// That is what happens below, literally. RWE already renders the world into a
// buffer of exactly twice the size when anti-aliasing is on, so each output
// pixel is one 2x2 block of it, which is the same shape the original filtered.
// The two top samples go through the table, the two bottom samples go through
// the table, and those two results go through the table again. Where a sample
// was not covered by a building it stands in as index 253 -- plain magenta,
// measured out of PALETTE.ALP as what the original's table found at the edge,
// beating the runner-up by more than half again (S:101). At the silhouette the
// pairs really are a real colour and whatever stood for transparent, exactly
// as they were in 1997, and the wrong colour falls out of the shipped table on
// its own.
//
// Two things follow from doing it this way rather than with a colour constant,
// and both are the point:
//
//   * The colour is per pixel and depends on the building's own edge. Read out
//     of the shipped table, a grey edge (128,128,128) comes back as index 148,
//     (167,123,179), a light purple; a near white edge comes back (211,171,215);
//     a pure green edge comes back (128,128,128), grey, not purple at all. The
//     "purple halo" is what that lookup averages to over the greys and metals
//     buildings are actually painted in -- an emergent property, not a fixed
//     colour, and painting one colour on every edge is a caricature of it.
//
//   * It lands where the original's lands. Only a MIXED block gets it: 1, 2 or
//     3 of the four samples covered. A block entirely inside the building is
//     ordinary anti-aliasing, which RWE's own supersample already does, and a
//     block entirely outside is terrain. So the artefact sits on the
//     building's own outermost pixels, the way the original's sat in the
//     building's cached bitmap -- a discoloured rim, not a glow thrown onto
//     the ground around it.
//
// One artefact is inherent to doing this from a screen-space mask and is worth
// knowing about before it is reported as a bug. The original anti-aliases each
// building's bitmap in isolation, so its halo follows its own outline and
// whatever stands in front is painted over the top. Here the mask is drawn
// depth-tested against the finished world, so the coverage also ends wherever
// something else occludes the building -- and a boundary is a boundary, so a
// tank parked in front of a factory picks up a thread of halo along the edge
// that overlaps it. Dropping the depth test trades that for the worse one: a
// building completely hidden behind a hill would draw its outline over the
// hill.
//
// 0 strength leaves it out. 1 is the original: the filtered pixel replaces
// what was there, because in TA this WAS the pixel.
uniform float haloStrength;

// Two adjustments applied to what the table returns, and they are RWE's, not
// the original's -- they are here because a play-test of the exact colours
// asked for something less saturated and redder than the table gives, and the
// person asking is the one who remembers what it looked like on a CRT.
//
// A word on why that is a reasonable thing to want rather than a fudge. The
// table's answers are correct as palette indices, but a palette index is not a
// colour until something displays it, and TA's were displayed on a 1997 CRT
// through a 256 entry LUT. Phosphor, a warmer white point and the gamma of
// that path all pull a saturated blue-purple towards a duller red-magenta, and
// none of that is in the data. So the arithmetic is left exact and the
// correction sits here, in one place, switched off by setting both to 100 and
// 0 -- which is the setting to use if you want to see what the table actually
// says.
//
// 1.0 saturation keeps the table's own; lower pulls each pixel towards its own
// luminance, so it desaturates without shifting hue.
uniform float haloSaturation;
// Pulls blue down towards green, which rotates the hue round from purple
// towards red-magenta. 0 leaves the table's hue alone; 1 brings blue all the
// way to green and leaves nothing magenta at all, so the useful range is the
// middle. It never RAISES blue, so a blend that is already green-leaning --
// the grey a green edge returns, for one -- is untouched.
//
// It has to work on the channels rather than by reordering them. The obvious
// version, sending the larger of red and blue into red, does nothing at all to
// the two harshest colours here, because they come out of the table with red
// and blue exactly equal: black blended with magenta is (128,0,128) and
// magenta blended with itself is (255,0,255), and those are the pixels that
// most look like a stray highlighter mark.
uniform float haloRedShift;

// Whether the box filter is applied only to what the original applied it to.
//
// Anti-aliasing here means rendering the world into a buffer of twice the
// size and averaging each 2x2 block down. That is the original's arithmetic,
// and the halo below is a consequence of it -- but the original applied it to
// exactly one thing: a **building's cached bitmap**. There is no supersampled
// world buffer in TA at all. The double-size buffer is allocated per
// building, filtered once and cached, which is precisely why a DONT_CACHE
// piece never carries a halo. The ground was never filtered, and neither was
// a moving unit.
//
// RWE reaches the same arithmetic from the other end, by supersampling the
// whole frame, so without this it filtered everything. That was reported from
// play twice: the ground first ("the terrain just looks blurry", which is a
// fair description of what averaging four reads of a palette-indexed map
// does -- the mean of four PALETTE.SHD samples names a colour the palette
// does not contain), and then the rest.
//
// So the filter is now selective. A 2x2 block is averaged if a cached
// building piece covers any of it, and otherwise takes a single sample --
// which is the pixel a render at native size would have produced. That gives
// buildings the original's treatment, silhouette included, and leaves the map
// and the units alone.
//
// Zero when there is nothing to select between: with anti-aliasing off the
// buffer is not supersampled, and the mask this reads is neither written nor
// cleared.
uniform float selectiveAntiAlias;

// ...and the one place a player can put back what the original did not do.
// Some people want smooth edges on their units more than they want 1997, and
// the map is not up for discussion either way -- the blur there was never
// anti-aliasing, it was a filter applied to a texture that cannot survive
// one. rwe.cfg key anti-alias-units, and a switch on the VISUALS page.
uniform float antiAliasUnits;

// What the original's table found where a building's edge met nothing.
const int TransparentIndex = 253;

// One entry of PALETTE.ALP. The table is symmetric in all 65536 pairs, so
// which operand is "source" and which is "destination" does not arise.
vec4 blend(int a, int b)
{
    return texelFetch(alphaTable, ivec2(a, b), 0);
}

// Alpha carries a blended entry's own index, for chaining into another lookup.
int indexOf(vec4 entry)
{
    return int((entry.a * 255.0) + 0.5);
}

// Anything at all was drawn here: a cached building piece or an occluder.
bool solid(vec4 texel)
{
    return texel.a > 0.25;
}

// ...and it was a cached piece of a finished building, the only thing that can
// carry the halo.
bool cached(vec4 texel)
{
    return texel.a > 0.75;
}

// A mask sample: red is the index, and a sample with nothing in it stands in as
// the transparent one, which is where the bug comes from.
int maskIndex(vec4 texel)
{
    return solid(texel) ? int((texel.r * 255.0) + 0.5) : TransparentIndex;
}

// The ground wrote this sample. Only mapTerrain.frag sets green.
bool terrain(vec4 texel)
{
    return texel.g > 0.5;
}

// Solid, and not the ground: a unit, a nanoframe, a modelled feature, or a
// dont-cache piece of a building. Everything the original drew straight to
// the screen without a double-size buffer anywhere near it.
bool unfiltered(vec4 texel)
{
    return solid(texel) && !terrain(texel) && !cached(texel);
}

void main(void)
{
    vec4 screenValue = texture(screenTexture, fragTexCoord);

    // The 2x2 block of the supersampled buffer this output pixel came from,
    // and its four mask samples. Both features below want them, so they are
    // read once. texelFetch and not texture(): the red channel is a palette
    // index, and the mean of two palette indices names a third colour that is
    // nowhere between them, so nothing here may be filtered.
    ivec2 block = ivec2(0, 0);
    vec4 s00 = vec4(0.0);
    vec4 s10 = vec4(0.0);
    vec4 s01 = vec4(0.0);
    vec4 s11 = vec4(0.0);
    if (selectiveAntiAlias > 0.0 || haloStrength > 0.0)
    {
        block = (ivec2(fragTexCoord * vec2(textureSize(buildingMask, 0))) / 2) * 2;
        s00 = texelFetch(buildingMask, block + ivec2(0, 0), 0);
        s10 = texelFetch(buildingMask, block + ivec2(1, 0), 0);
        s01 = texelFetch(buildingMask, block + ivec2(0, 1), 0);
        s11 = texelFetch(buildingMask, block + ivec2(1, 1), 0);
    }

    if (selectiveAntiAlias > 0.0)
    {
        // A cached building piece anywhere in the block is what the original
        // filtered, and the block is averaged for it -- its silhouette
        // included, which is where the halo comes from and why the two have
        // to agree about which blocks those are.
        bool filterThis = cached(s00) || cached(s10) || cached(s01) || cached(s11);

        // And the player's own answer to the question the original never
        // asked, for everything solid that is not the ground.
        if (antiAliasUnits > 0.0)
        {
            filterThis = filterThis || unfiltered(s00) || unfiltered(s10) || unfiltered(s01) || unfiltered(s11);
        }

        if (!filterThis)
        {
            screenValue = texelFetch(screenTexture, block, 0);
        }
    }

    vec4 dodgeMaskValue = texture(dodgeMask, fragTexCoord);
    vec3 dodged = screenValue.rgb / (vec3(1.0, 1.0, 1.0) - dodgeMaskValue.rgb);

    if (haloStrength > 0.0)
    {
        // Mixed against the WORLD, not against the building: the block has to
        // straddle the outer edge of everything solid. Counting only the
        // cached samples instead would find a boundary wherever an occluder
        // crosses a building -- a rotating arm over its own base, a tank in
        // front of a factory -- and paint a line there, inside the model.
        int solidCount = (solid(s00) ? 1 : 0) + (solid(s10) ? 1 : 0) + (solid(s01) ? 1 : 0) + (solid(s11) ? 1 : 0);
        // And every solid sample in it has to be a cached building piece. An
        // occluder's own outer edge is a real silhouette, but it is not one
        // the original's table ever saw, so it gets nothing.
        bool allCached = (!solid(s00) || cached(s00))
            && (!solid(s10) || cached(s10))
            && (!solid(s01) || cached(s01))
            && (!solid(s11) || cached(s11));

        if (solidCount > 0 && solidCount < 4 && allCached)
        {
            // The original's three lookups, in the original's order.
            int top = indexOf(blend(maskIndex(s00), maskIndex(s10)));
            int bottom = indexOf(blend(maskIndex(s01), maskIndex(s11)));
            vec3 halo = blend(top, bottom).rgb;

            // RWE's two corrections, below the line where the arithmetic ends.
            halo.b = mix(halo.b, min(halo.b, halo.g), haloRedShift);
            halo = mix(vec3(dot(halo, vec3(0.299, 0.587, 0.114))), halo, haloSaturation);

            dodged = mix(dodged, halo, haloStrength);
        }
    }

    outColor = vec4(clamp(dodged * gamma, 0.0, 1.0), 1.0);
}
