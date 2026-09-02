#version 150

in vec2 fragTexCoord;
in float height;
in vec3 worldNormal;
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
// The original has no model lighting at all. The full unit draw chain was
// decoded and no normal is ever computed, no sun vector exists, and the
// span fillers copy texels untouched: every unit texel draws at 1.0. The
// shading players remember is painted by the artists -- a solar collector
// binds bright and dark variants of the same metal texture per face
// orientation, and one texture is literally named 32XGouraud. Earlier
// guesses at a sun direction here made mirrored faces of one building come
// out at different brightnesses; the truth is simpler.

void main(void)
{
    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    outColor = vec4(vec3(baseColor) * (height > seaLevel ? normalTint : waterTint), alpha);
}
