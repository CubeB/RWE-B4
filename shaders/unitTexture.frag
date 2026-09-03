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
// TA lights its models after all. The renderer decoded earlier -- no normals,
// no sun, texels copied unmodified -- is the one that runs with SHADING
// switched OFF; the option defaults ON, and 0x458744 picks between two
// complete rasterizer chains on that one bit. The shaded chain computes
// per-face normals, averages them per vertex, and takes
//
//     level = (int)(5.0 * dot(n, (-0.8, 1.0, 0.25))) & 0x1F
//
// as a row of PALETTE.SHD, whose row k multiplies the palette by 0.06875k --
// so row ~14.55 is identity and five rows separate each unit of the dot.
//
// Where RWE departs: the original's `& 0x1F` WRAPS. A surface perpendicular
// to the sun lands on row 0, pure black, and a replay against the stock
// models puts 8% of visible pixels there with 46% of quads straddling the
// wrap. That is the original being wrong rather than subtle, so the same sun
// and the same per-row step are centred on the identity row instead of
// wrapped, and clamped to the table's ends.
// The exe's sun is (-0.8, 1, +0.25); the z is negated here because RWE's
// world z runs south where the original's runs north.
const vec3 lightDirection = normalize(vec3(-0.8, 1.0, -0.25));
const float shadeRowMultiplier = 0.06875;

// The original's arithmetic, wrap and all (0x459C70):
//
//     level = (int)(5.0 * dot(nInward, L)) & 0x1F
//
// picking a row of PALETTE.SHD, where row k multiplies the palette by
// 0.06875k. The exe's cross product yields the INWARD normal, so RWE's
// outward one is negated before the dot.
//
// The `& 0x1F` was read as a bug once and replaced here with a centred,
// monotone ramp -- on the grounds that a face perpendicular to the sun wraps
// to row 0 and comes out pure black. A screenshot of the original settled
// it: that black is the look. On ARMSOLAR the wrap gives the left panel row
// 28 (1.93x, washed out) and the right panel row 1 (0.069x, black), which is
// exactly what the original draws -- one lit panel and one solid black one.
// The centred ramp gave 1.94x and 0.68x, two panels that read the same. The
// wrap is not the original being crude; it is the original's contrast.
float shadeIntensity(vec3 normal)
{
    int level = int(5.0 * dot(-normalize(normal), lightDirection)) & 31;
    return shadeRowMultiplier * float(level);
}

void main(void)
{
    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    float intensity = shade ? shadeIntensity(worldNormal) : 1.0;
    outColor = vec4(vec3(baseColor) * intensity * (height > seaLevel ? normalTint : waterTint), alpha);
}
