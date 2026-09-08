#version 150

uniform mat4 vpMatrix;
uniform mat4 modelMatrix;
uniform float groundHeight;

// Which of the original's two shadow passes this model goes through.
//
// TA has two, and sorts a model into one of them on unit+0x113 bit 5
// (TOTALA-EXE.md S:100). A building's shadow is genuinely projected: 0x45A790
// sizes a drawable and fills it flat, which is why it could be RLE'd and why
// cutting the building's own outline back out of it was a separate step. A
// unit's is not projected at all -- 0x45A470 copies the bitmap the unit was
// already cached into, rep movs, width*height bytes, no transform of any kind,
// and blits it at an offset. So a tank's shadow is its own silhouette moved
// across the screen, and it does not stretch however tall the tank is.
//
// This shader can reproduce both because the world projection is orthographic:
// a constant translation in world space is a constant translation on screen,
// so displacing every vertex by the same vector carries the silhouette across
// unchanged, which is exactly what blitting the cached bitmap does. The pass
// runs with the depth buffer off and writes only the stencil, so nothing else
// has to agree about where the geometry sits.
uniform bool projected;

// For the offset kind, the one height the displacement is taken from. The
// projected kind takes it per vertex and ignores this.
//
// The original's offset is not decoded -- 0x4B8500 is a tree walk and the blit
// it reaches was not followed -- so this is RWE's own choice, made to keep the
// shadow where the eye already expects it: the unit's base plus half its model
// height is about where the mean of the old per-vertex shear fell, so the
// change reads as the shadow no longer stretching rather than as it jumping
// somewhere new. It rises with the unit, so an aircraft's shadow still walks
// away from it as it climbs.
uniform float shadowOriginY;

in vec3 position;
in vec2 texCoord;

out vec2 fragTexCoord;
out float height;

void main(void)
{
    vec4 worldPosition = modelMatrix * vec4(position, 1.0);

    vec4 shadowPosition;
    if (projected)
    {
        float shadowOffset = (worldPosition.y - groundHeight) * 0.25;
        shadowPosition = vec4(
            worldPosition.x + shadowOffset,
            groundHeight,
            worldPosition.z - shadowOffset,
            1.0);
    }
    else
    {
        // Note the sign on z, which is not the projected branch's. This camera
        // puts screen-up at 0.5y - z, so the projected branch gets its downward
        // component from flattening y to the ground and its sideways one from
        // -z, netting a displacement of (+s, +s) down and to the right. Keeping
        // y means that first component is gone, and -z alone would carry the
        // shadow UP the screen. +z restores it: with y held, (x + s, y, z + s)
        // is the same (+s, +s) on screen, so both kinds of shadow fall the same
        // way and only their shape differs.
        float shadowOffset = (shadowOriginY - groundHeight) * 0.25;
        shadowPosition = vec4(
            worldPosition.x + shadowOffset,
            worldPosition.y,
            worldPosition.z + shadowOffset,
            1.0);
    }

    gl_Position = vpMatrix * shadowPosition;
    fragTexCoord = texCoord;
    height = worldPosition.y;
}
