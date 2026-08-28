#version 150

in vec2 fragTexCoord;
in float height;
in vec3 worldNormal;
out vec4 outColor;

uniform sampler2D textureSampler;
uniform float seaLevel;
uniform bool shade;

const vec3 waterTint = vec3(0.5, 0.5, 1.0);
const vec3 normalTint = vec3(1.0, 1.0, 1.0);
// The sun sits low to the left and slightly in front, as in TA: faces that
// look left are brightest, tops a little dimmer, right-facing sides dark.
const vec3 lightDirection = normalize(vec3(-1.2, 1.0, 0.4));

void main(void)
{
    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    float lightAngleFactor = clamp(dot(worldNormal, lightDirection), 0.0, 1.0);
    float lightIntensity = shade
        ? 0.4 + 0.7 * clamp(dot(worldNormal, lightDirection), 0.0, 1.0)
        : 1.0;
    outColor = vec4(vec3(baseColor) * lightIntensity * (height > seaLevel ? normalTint : waterTint), 1.0);
}
