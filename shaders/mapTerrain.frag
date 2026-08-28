#version 150

in vec3 fragTexCoord;
in vec2 fragWorldPos;
out vec4 outColor;

uniform sampler2DArray textureArraySampler;

// Fog of war, one texel per sight cell. Alpha: 0 = in view, ~0.5 = explored, 1 = never seen.
uniform sampler2D fogSampler;
uniform bool fogEnabled;
// xy: world position of the fog map's top-left corner; zw: 1 / fog map size in world units.
uniform vec4 fogTransform;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main(void)
{
    vec4 color = texture(textureArraySampler, fragTexCoord);

    if (fogEnabled)
    {
        // TA's fog boundary follows its 32-unit sight grid, eroded by a few
        // pixels of fixed noise. Sampling the coarse map at a position that
        // is jittered per 2x2 pixel block gives the same ragged, stable edge.
        vec2 block = floor(fragWorldPos / 2.0);
        vec2 jitter = vec2(hash(block), hash(block + vec2(17.0, 43.0))) * 8.0 - 4.0;
        vec2 uv = (fragWorldPos + jitter - fogTransform.xy) * fogTransform.zw;
        float fog = texture(fogSampler, uv).a;
        if (fog > 0.9)
        {
            color = vec4(0.0, 0.0, 0.0, 1.0);
        }
        else if (fog > 0.3)
        {
            // Explored ground is remembered in grey, as TA draws it.
            float grey = dot(color.rgb, vec3(0.299, 0.587, 0.114)) * 0.7;
            color = vec4(grey, grey, grey, 1.0);
        }
    }

    outColor = color;
}
