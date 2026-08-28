#version 150

in vec2 fragTexCoord;
in float height;
in vec3 worldNormal;
out vec4 outColor;

uniform sampler2D textureSampler;
uniform float unitY;
uniform float seaLevel;
uniform bool shade;
uniform float percentComplete;
uniform float time;
// Height of the whole model above unitY.
uniform float unitHeight;

const vec3 waterTint = vec3(0.5, 0.5, 1.0);
const vec3 normalTint = vec3(1.0, 1.0, 1.0);
// The sun sits low to the left and slightly in front, as in TA: faces that
// look left are brightest, tops a little dimmer, right-facing sides dark.
// Must match unitTexture.frag.
const vec3 lightDirection = normalize(vec3(-1.3, 1.0, 0.3));
const float ambientLight = 0.58;
const float directionalLight = 0.6;

// Construction happens in three equal phases:
//   [0, 1/3)   nothing but the wireframe (drawn separately)
//   [1/3, 2/3) the whole model in pulsing green
//   [2/3, 1]   the texture sweeps up from the base to the top of the model
const float greenPhaseStart = 1.0 / 3.0;
const float texturePhaseStart = 2.0 / 3.0;
// Brighter green just above the advancing texture front.
const float textureLeadThickness = 8.0;

vec3 shadeNormal()
{
    vec3 baseColor = vec3(texture(textureSampler, fragTexCoord));
    float lightIntensity = shade
        ? ambientLight + directionalLight * clamp(dot(worldNormal, lightDirection), 0.0, 1.0)
        : 1.0;
    return baseColor * lightIntensity * (height > seaLevel ? normalTint : waterTint);
}

void main(void)
{
    float posY = height - unitY;

    //0 = transparent, 1 = sine green, 2 = cosine green, 3 = textured
    int shadingMethod = 0;

    if (percentComplete >= texturePhaseStart)
    {
        // The texture front climbs from the bottom of the model to the top
        // over the final third of the build.
        float textureProgress = (percentComplete - texturePhaseStart) / (1.0 - texturePhaseStart);
        float frontHeight = textureProgress * max(unitHeight, 1.0);
        if (posY <= frontHeight)
            shadingMethod = 3;
        else if (posY <= frontHeight + textureLeadThickness)
            shadingMethod = 2;
        else
            shadingMethod = 1;
    }
    else if (percentComplete >= greenPhaseStart)
    {
        shadingMethod = 1;
    }

    //Now we actually compute pixel color
    vec4 color = vec4(0,0,0,1);

    //Shading methods very simple right now, however can be expanded upon
    //for more complex effects at high fidelity.
    //I tried to emulate the TA color movement but my attempts looked bad.
    //Just pure green ended up looking better.

    float time2 = time / 13.0;

    float lightIntensity = (0.5 * clamp(dot(worldNormal, lightDirection), 0.0, 1.0)) + 0.5;

    if (shadingMethod == 3) {
        color = vec4(shadeNormal(), 1.0);
    }
    else if (shadingMethod == 0)
        color.a = 0;
    else if (shadingMethod == 1) {
        color.g = ((0.5*sin(time2))+0.5) * lightIntensity;
    }
    else if (shadingMethod == 2) {
        color.g = ((0.5*cos(time2))+0.5) * lightIntensity;
    }

    outColor = color;
}
