#version 150

in vec2 fragTexCoord;
out vec4 outColor;
// The building halo's coverage mask, written by this variant of
// basicTexture.frag so that a standing feature -- a tree, a rock, anything
// tall enough to stand in front of a building -- counts as an occluder. See
// unitTexture.frag and worldPost.frag for the convention: red unused, alpha
// 0.5 for "solid, but not a building", which is all a sprite ever writes
// here. Without this a tree in front of a building left the mask reading
// "building" at every pixel the tree had actually overdrawn, and the halo
// pass painted its purple fringe into the tree's own colour (issue #67).
out vec4 outMask;

uniform sampler2D textureSampler;
uniform vec4 tint;
// 1 draws the sprite in the grey of remembered-but-unseen ground; 0 draws it normally.
uniform float desaturate;
// Always 0.5 -- "anything else solid" -- for the one caller that wants a
// mask at all. A uniform rather than a constant so this file stays a plain
// copy of basicTexture.frag with the mask bolted on, not a special case.
uniform float maskValue;

void main(void)
{
    vec4 texColor = texture(textureSampler, fragTexCoord);

    // A sprite is a billboard with real transparent margin around its
    // silhouette, unlike basicTexture.frag's other callers this shader does
    // not serve. Discarding below half, the same cutoff unitTexture.frag
    // uses for a model's own alpha, keeps the mask to the tree's outline
    // instead of the whole quad.
    //
    // The test is on the texture's own alpha rather than on the tinted
    // colour, because the tint is where translucency lives: a translucent
    // sprite is drawn with a tint alpha of 0.5, which would put every one of
    // its opaque texels exactly on this cutoff and discard anything fainter.
    // The silhouette is the texture's business; the tint only says how
    // strongly to draw it.
    if (texColor.a < 0.5)
    {
        discard;
    }

    vec4 color = texColor * tint;

    float grey = dot(color.rgb, vec3(0.299, 0.587, 0.114)) * 0.7;
    outColor = vec4(mix(color.rgb, vec3(grey), desaturate), color.a);
    outMask = vec4(0.0, 0.0, 0.0, maskValue);
}
