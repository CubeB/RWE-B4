#version 150

uniform mat4 mvpMatrix;
uniform mat4 modelMatrix;

in vec3 position;
in vec2 texCoord;
in vec3 normal;

out vec2 fragTexCoord;
out float height;

// The shade level, computed HERE and interpolated across the face.
//
// That placement is the whole point. The original computes the level once per
// vertex (0x45A2AA-0x45A2F3), masks it there, and then Gouraud-interpolates
// the resulting integer ROW down the edges and along each scanline as a 16.16
// fixed-point interpolant -- there is no mask and no clamp anywhere in the
// span fillers. Computing the level per PIXEL from an interpolated normal, as
// this shader used to, is a different operation: across the wrap the original
// ramps smoothly from row 29 down through the middle of the table to row 0,
// while a per-pixel level snaps straight from white to black.
out float shadeLevel;

const vec3 sunDirection = vec3(-0.8, 1.0, -0.25);

void main(void)
{
    vec4 worldPosition = modelMatrix * vec4(position, 1.0);
    gl_Position = mvpMatrix * vec4(position, 1.0);
    fragTexCoord = texCoord;
    height = worldPosition.y;
    // The exe's own arithmetic, at the vertex where it belongs:
    //
    //     level = (int)(5.0 * dot(nInward, L)) & 0x1F
    //
    // Neither the normal nor the light is normalised. `normal` is the mean of
    // the unit face normals meeting at this vertex, so it is shorter than one
    // wherever those faces disagree, and L is (-0.8, 1, 0.25) with a length of
    // 1.3048 -- together they set the width of the ramp, about thirteen rows
    // of the thirty-two. Normalising either one widens it and blows the
    // contrast out. The z is negated because RWE's world z runs south where
    // the original's runs north, and RWE's face normals point outward where
    // the exe's cross product yields the inward one.
    vec3 averagedNormal = mat3(modelMatrix) * normal;
    shadeLevel = float(int(5.0 * dot(-averagedNormal, sunDirection)) & 31);
}
