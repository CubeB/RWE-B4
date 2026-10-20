#version 150

in vec2 fragTexCoord;
in float height;
out vec4 outColor;

uniform float groundHeight;

// A mobile unit whose origin is below the sea has its copied silhouette cut
// below the water line (SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md section 2),
// so a submerged unit can cast no shadow at all. The original encodes the
// threshold as `sea level - H + 50` with the height buffer's bias of 50, which
// is this comparison against the fragment's own world height. NoCutBelowY
// leaves a projected building shadow alone.
uniform float cutBelowY;

uniform sampler2D textureSampler;

void main(void)
{
    if (height < groundHeight)
    {
        discard;
    }

    if (height < cutBelowY)
    {
        discard;
    }

    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    outColor = vec4(0.0, 0.0, 0.0, 1.0);
}
