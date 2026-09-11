#version 150

in vec2 fragTexCoord;
in float height;
in float shadeLevel;
out vec4 outColor;
// The building halo's coverage mask, filled as a second render target while
// this pass draws. It used to be a separate pass over the whole world, which
// was a second walk of every model's pieces and a second draw call each to
// recover two things this shader already has in hand: the texel's palette
// index, which it samples below for the shade lookup, and which surface is in
// front, which the depth test has already settled. Measured on a 200 v 200
// battle_test that cost about 740us a frame and 3300 draw calls. Here it is a
// register write. See worldPost.frag and TOTALA-EXE.md S:101.
//
// Red is the palette index. Alpha says what kind of sample it is: 1 for a
// cached piece of a finished building, the only thing that can carry a halo,
// 0.7 for a finished building's dont-cache piece, which is anti-aliased with
// its building but carries no halo, and 0.5 for anything else solid, which is
// coverage without being a source. Cleared 0 means nothing is there.
out vec4 outMask;
// 1.0, 0.7 or 0.5 as above, set per mesh. See RenderService::drawUnitMeshBatch.
uniform float maskValue;

uniform sampler2D textureSampler;
// The same atlas again at the same coordinates, one byte a texel: that texel's
// raw palette index. Nearest-filtered, with a mip chain built by picking a
// representative index rather than averaging, because the mean of two palette
// indices names a third colour that is nowhere between them.
uniform sampler2D paletteIndexSampler;
// palettes/PALETTE.SHD as a 256x32 image: column t of row r is the colour of
// SHD[r * 256 + t].
uniform sampler2D shadeTableSampler;
uniform float seaLevel;
uniform float shadeStrength;
// How much of its own colour the model keeps where it covers the screen. 1 for
// everything except a cloaked unit, which the original averages with whatever
// is behind it. See RenderService::drawUnitMeshBatch.
uniform float alpha;

const vec3 waterTint = vec3(0.5, 0.5, 1.0);
const vec3 normalTint = vec3(1.0, 1.0, 1.0);

// TA lights its models after all -- its buildings and features, that is. The
// renderer decoded earlier -- no normals, no sun, texels copied unmodified --
// is the one that runs with SHADING switched OFF, and the one every mobile
// unit gets regardless: 0x4586A0 sends only a building (bmcode 0) to the
// shaded chain, and only with SHADING on (TOTALA-EXE-SHADING.md S:11). RWE's
// Shading switch defaults to that, Buildings, and can shade units as well.
// The shaded chain averages the
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
// What a row MEANS is now the table itself rather than a curve fitted to it.
// This used to multiply the texel by a 32-entry array of measured brightness
// ratios, each being the mean of what that row does to every palette entry
// bright enough to carry one. The mean is the part that could not be right:
// PALETTE.SHD stores palette INDICES from a nearest-neighbour search, so a row
// does not scale a colour, it snaps the scaled colour back onto the 256 entries
// that exist. Entry 250, a pure green, comes back unchanged at every row from
// 14 to 31 while a mid grey brightens by 80%, and at row 31 fifty-eight of the
// 256 entries have run out of palette to move into and land on white. No single
// multiplier reproduces that, which is why the texel's own index is now carried
// through the atlas and the colour is read straight out of the table.
vec3 shadeTexel(vec3 unshaded)
{
    if (shadeStrength <= 0.0)
    {
        return unshaded;
    }

    // The original's inner loop entire, 0x4C81A4-0x4C81BD:
    //
    //     texel = texture[(v >> 16) * width + (u >> 16)]   ; raw palette index
    //     row   = shade >> 16                              ; truncated, not rounded
    //     pixel = PALETTE.SHD[row * 256 + texel]
    //
    // One shift, one add, one byte load. Between the shift and the load there
    // is no second mask, no clamp, no ambient term, no fog and no blend
    // between neighbouring rows -- verified at the byte level, and the only
    // mask in the flat-colour filler beside it lands on the colour index
    // rather than on the row. So each gradient is quantised into whole rows
    // and a face steps down the table a band at a time; the bands are the
    // original's look. Both ends of the interpolation are masked rows in
    // 0..31, so the value cannot leave the table and the clamp here is
    // against the array bound alone.
    int row = clamp(int(shadeLevel), 0, 31);
    int texel = int((texture(paletteIndexSampler, fragTexCoord).r * 255.0) + 0.5);
    vec3 tableColor = texelFetch(shadeTableSampler, ivec2(texel, row), 0).rgb;

    // Strength blends the table towards "not shaded at all". At 1.0 -- the
    // default -- this is the table exactly, row 0 included, which is genuinely
    // black; the original's is too. Below 1.0 it is the same lookup with its
    // contrast pulled in around the unshaded colour, an rwe.cfg option for
    // anyone who wants the model to read more softly than the original draws
    // it. The unshaded colour is the texture as authored, which is what the
    // exe hands a piece the COB has said DONT_SHADE on -- near enough the same
    // thing as its row 15, which is the identity for 232 of the 256 entries
    // and within 12/255 of it for the rest.
    return mix(unshaded, tableColor, shadeStrength);
}

void main(void)
{
    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    vec3 lit = shadeTexel(vec3(baseColor)) * (height > seaLevel ? normalTint : waterTint);
    outColor = vec4(min(lit, vec3(1.0)), alpha);
    outMask = vec4(texture(paletteIndexSampler, fragTexCoord).r, 0.0, 0.0, maskValue);
}
