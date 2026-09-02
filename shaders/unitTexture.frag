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
// The sun sits low to the left and slightly in front, as in TA: faces that
// look left are brightest, tops a little dimmer, right-facing sides dark.
//
// The falloff is wrapped rather than clamped — the dot product is remapped
// from -1..1 into 0..1 instead of being cut off at zero — so a face turned
// away from the sun shades off gradually instead of dropping straight to the
// ambient floor. Clamping put every away-facing surface at 0.58 while every
// mobile unit (which is not shaded at all) sat at 1.0, so a solar collector
// read as black next to a tank parked beside it, and its own two mirrored
// panels came out at 1.10 and 0.58 despite carrying the same texture.
// TA's artists already baked their shading into the choice of texture, so
// the engine only needs to suggest a light, not impose one.
//
// A left-facing wall gets ~1.07x the texture colour, a flat top ~0.99x, a
// right-facing wall ~0.75x, and a face tilted up-left towards the sun ~1.08x.
// Must match unitBuild.frag.
const vec3 lightDirection = normalize(vec3(-1.3, 1.0, 0.3));
const float ambientLight = 0.72;
const float directionalLight = 0.36;

void main(void)
{
    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    float lightIntensity = shade
        ? ambientLight + directionalLight * (0.5 + 0.5 * dot(normalize(worldNormal), lightDirection))
        : 1.0;
    outColor = vec4(vec3(baseColor) * lightIntensity * (height > seaLevel ? normalTint : waterTint), alpha);
}
