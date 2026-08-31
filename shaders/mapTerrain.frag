#version 150

in vec3 fragTexCoord;
in vec2 fragWorldPos;
out vec4 outColor;

uniform sampler2DArray textureArraySampler;

// Fog of war, one texel per world unit, rasterised from TA's own fog.gaf tiles.
// Red channel: 0 = in view, ~0.5 = explored, 1 = never seen.
uniform sampler2D fogSampler;
uniform bool fogEnabled;
// xy: world position of the fog map's top-left corner; zw: 1 / fog map size in world units.
uniform vec4 fogTransform;

void main(void)
{
    vec4 color = texture(textureArraySampler, fragTexCoord);

    if (fogEnabled)
    {
        // The fog map is indexed in the projected space that TA places its
        // vision cells in: GameSimulation::visionCellAt takes a world point
        // (x, y, z) to the row (z - H/2), H being the ground height there,
        // because the cabinet camera draws it at that screen row.
        //
        // The terrain graphic *is* that projection. It is drawn as a flat
        // sheet at y = 0, so its own fragment at (x, z) has H contributing
        // nothing and the transform reduces to the identity: the fragment's
        // vision cell is the one at (x, z), and this plain lookup is that
        // same transform, not an approximation of it.
        //
        // The ragged boundary comes from the artwork, so no jitter here.
        vec2 uv = (fragWorldPos - fogTransform.xy) * fogTransform.zw;
        float fog = texture(fogSampler, uv).r;
        if (fog > 0.75)
        {
            color = vec4(0.0, 0.0, 0.0, 1.0);
        }
        else if (fog > 0.25)
        {
            // Explored ground is remembered in grey, as TA draws it.
            float grey = dot(color.rgb, vec3(0.299, 0.587, 0.114)) * 0.7;
            color = vec4(grey, grey, grey, 1.0);
        }
    }

    outColor = color;
}
