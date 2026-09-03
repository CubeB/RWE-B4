#version 150

in vec2 fragTexCoord;
in float height;
in vec3 worldNormal;
out vec4 outColor;

uniform sampler2D textureSampler;
uniform float unitY;
uniform float seaLevel;
uniform bool shade;
// Height of the whole model above unitY.
uniform float unitHeight;

// The construction display, as the original draws it. It keeps an 8-bit
// height per pixel — the model-space Y of the topmost surface there, plus 50 —
// and remaps every pixel by comparing that against a moving threshold. Above
// the line, in a four-unit band on it, and below it are each drawn one of
// four ways, and which three ways depends on how far the build has got.
uniform float buildRatio;
// 0 = not drawn, 1 = build colour A, 2 = build colour B, 3 = the texture.
uniform int aboveMode;
uniform int bandMode;
uniform int belowMode;
// Two triangle waves over palette entries 160..175 at different rates.
uniform vec3 buildColorA;
uniform vec3 buildColorB;

// The band on the line is four of the original's height units tall.
const float bandThickness = 4.0;
// The original's height buffer is the model-space Y biased by 50.
const float heightBias = 50.0;

const vec3 waterTint = vec3(0.5, 0.5, 1.0);
const vec3 normalTint = vec3(1.0, 1.0, 1.0);
// The shaded chain's lighting; see unitTexture.frag, which this must match.
const vec3 lightDirection = normalize(vec3(-0.8, 1.0, 0.25));
const float shadeIdentityRow = 14.5455;
// The exe's own slope is 5 rows per unit of the dot, but with its wrap a
// lit face lands on rows ~21-22 (1.45-1.55x) and an away face far darker;
// centred and clamped, 5 was too flat. 8 reaches the measured lit and dark
// levels without the wrap's black band at perpendicular.
const float shadeRowsPerUnitDot = 8.0;
const float shadeRowMultiplier = 0.06875;

float shadeIntensity(vec3 normal)
{
    float row = shadeIdentityRow + (shadeRowsPerUnitDot * dot(normalize(normal), lightDirection));
    return shadeRowMultiplier * clamp(row, 0.0, 31.0);
}

vec3 shadeNormal()
{
    vec3 baseColor = vec3(texture(textureSampler, fragTexCoord));
    float intensity = shade ? shadeIntensity(worldNormal) : 1.0;
    return baseColor * intensity * (height > seaLevel ? normalTint : waterTint);
}

void main(void)
{
    // Where this fragment sits in the original's height buffer. It keeps the
    // topmost surface per pixel; shading each fragment by its own height is
    // the same thing wherever the model does not overhang itself, and is
    // better behaved where it does.
    float z = (height - unitY) + heightBias;

    int mode;
    if (z >= buildRatio)
    {
        mode = aboveMode;
    }
    else if (z >= buildRatio - bandThickness)
    {
        mode = bandMode;
    }
    else
    {
        mode = belowMode;
    }

    if (mode == 0)
    {
        // Erased: this part of the model has not been laid down yet.
        outColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
    else if (mode == 1)
    {
        outColor = vec4(buildColorA, 1.0);
    }
    else if (mode == 2)
    {
        outColor = vec4(buildColorB, 1.0);
    }
    else
    {
        outColor = vec4(shadeNormal(), 1.0);
    }
}
