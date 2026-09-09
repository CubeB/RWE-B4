#version 150

in vec2 fragTexCoord;
out vec4 outColor;

uniform sampler2D textureSampler;
// The same atlas again, one byte a texel, holding that texel's index into the
// game palette. It exists for the PALETTE.SHD shade lookup in unitTexture.frag
// and is exactly what this pass needs too.
uniform sampler2D paletteIndexSampler;

// The building's own pixels, as PALETTE INDICES rather than as coverage.
//
// This is what makes the halo the original's rather than an impression of it.
// TA anti-aliased a building by rendering it into a buffer twice the size in
// each dimension and filtering it down THROUGH A PALETTE TABLE -- so what it
// averaged were palette indices, and what came back was another palette index.
// A coverage mask can only say where an edge is; to reproduce what the table
// did there you have to carry the indices themselves to the place the
// downsample happens, which is worldPost.frag.
//
// Red is the index. Alpha says what kind of sample this is, in three states:
//
//   0.0  nothing here -- the buffer as it was cleared. worldPost reads this as
//        "transparent" and substitutes index 253, which is what the original's
//        table found at a building's edge.
//   0.5  an OCCLUDER: something solid, but not part of the cached bitmap, so
//        it cannot carry the halo. Dont-cache pieces and every other unit go
//        in at this level.
//   1.0  a cached piece of a finished building, the only thing that can.
//
// The middle state exists because of a specific artefact. The mask is depth
// tested, so anything in front of a building removes the building's samples
// there; if only the cached pieces were drawn, a metal extractor's rotating
// arm would punch a moving hole in its own base's coverage and the post pass
// would read the rim of that hole as a silhouette -- a purple line inside the
// model, crawling as the arm turns. Drawing the occluders fills the hole, so
// the boundary disappears and only the model's true outer edge is left.
//
// See AlphaTable.h, worldPost.frag and S:101.
uniform float maskAlpha;

void main(void)
{
    if (texture(textureSampler, fragTexCoord).a < 0.5)
    {
        discard;
    }

    outColor = vec4(texture(paletteIndexSampler, fragTexCoord).r, 0.0, 0.0, maskAlpha);
}
