#version 150

in vec2 fragTexCoord;
out vec4 outColor;

uniform sampler2D textureSampler;

// Coverage, and nothing else: 1 where the model is, whatever was cleared
// everywhere else. The texture is sampled only for its alpha, so a model's own
// see-through parts leave a hole in the mask exactly as they do on screen.
//
// This exists to find the silhouette EDGE. worldPost samples the result with
// the same linear filter that does the supersampled downsample, so a pixel
// that straddles the outline comes back partly covered, and that is where the
// original's purple halo lives -- see the halo comment in worldPost.frag.
void main(void)
{
    if (texture(textureSampler, fragTexCoord).a < 0.5)
    {
        discard;
    }

    outColor = vec4(1.0, 1.0, 1.0, 1.0);
}
