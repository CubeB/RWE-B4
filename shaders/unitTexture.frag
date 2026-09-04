#version 150

in vec2 fragTexCoord;
in float height;
in float shadeLevel;
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
// PALETTE.SHD's row k remaps each texel to the nearest palette entry to
// `colour * 0.06875k`, and row 15 is the identity -- the constant the exe
// hard-codes for a piece the COB has told not to shade. A plain multiply with
// a per-channel clamp reproduces that table to within about 5-7 of 255 over
// the real texel population of the stock unit textures; the clamp is not
// optional, since it is where all the bright-end behaviour comes from. The
// exact table is a nearest-neighbour remap in a 256-entry palette, which would
// need each texel's palette index carried through the atlas to reproduce
// faithfully; that is written up as still to do.
// Two deliberate departures from the original here, both asked for after
// looking at the two side by side, and both easy to put back.
//
// The original truncates the interpolated row to an integer per pixel, which
// quantises every gradient into at most thirty-two bands; dropping the floor
// keeps the interpolation continuous and the transition into shadow smooth.
//
// And row 0 is pure black, so a face that wraps to it goes to nothing at all.
// A shadow that keeps a little light in it reads better on a modern display
// and still leaves the lit end where the table puts it: the floor lifts the
// dark end and the scale is chosen so an unlit face (row 15, x1.031) comes
// out where it always did.
const float shadowFloor = 0.16;

float shadeIntensity()
{
    if (!shade)
    {
        return 1.0;
    }
    float tableValue = 0.06875 * clamp(shadeLevel, 0.0, 31.0);
    return shadowFloor + ((1.0 - shadowFloor) * tableValue);
}

void main(void)
{
    vec4 baseColor = texture(textureSampler, fragTexCoord);
    if (baseColor.a < 0.5)
    {
        discard;
    }

    vec3 lit = vec3(baseColor) * shadeIntensity() * (height > seaLevel ? normalTint : waterTint);
    outColor = vec4(min(lit, vec3(1.0)), alpha);
}
