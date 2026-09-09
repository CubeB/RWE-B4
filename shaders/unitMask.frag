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
// Red is the index, scaled to 0..1 the way an 8 bit channel scales, and alpha
// is coverage: 1 here, 0 wherever the buffer was cleared. worldPost reads a
// zero alpha as "this sample was transparent" and substitutes index 253, which
// is what the original's table found there. See AlphaTable.h and S:101.
void main(void)
{
    if (texture(textureSampler, fragTexCoord).a < 0.5)
    {
        discard;
    }

    outColor = vec4(texture(paletteIndexSampler, fragTexCoord).r, 0.0, 0.0, 1.0);
}
