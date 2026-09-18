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

// For the offset kind, the unit's base: how far it sits above groundHeight is
// how far its shadow drops. The projected kind takes height per vertex and
// ignores this.
//
// The offset is decoded (TOTALA-EXE.md S:100). The unit is drawn at its
// screen x plus 0x80 (0x4597BA) and the copy of its bitmap at plus 0x85
// (0x45933D), so the shadow sits five pixels to the right. Its screen y is
// the ground's under the unit, not the unit's, so it drops by the unit's
// height above the ground: nothing for anything that drives, and an
// aircraft's shadow lands on the ground below it and walks away as it climbs.
uniform float shadowOriginY;

// The original's five pixels: at its scale, one world unit is one pixel across
// the screen, so a zoomed view scales the shadow with everything else.
const float ShadowShiftX = 5.0;

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
        // This camera puts screen-up at 0.5y - z, so lowering every vertex by
        // the unit's height above the ground moves the silhouette down the
        // screen by half that -- the original's ground-y minus unit-y, whose
        // halving is the same projection's. z is left alone: moving it as
        // well would move the shadow a second time.
        shadowPosition = vec4(
            worldPosition.x + ShadowShiftX,
            worldPosition.y - (shadowOriginY - groundHeight),
            worldPosition.z,
            1.0);
    }

    gl_Position = vpMatrix * shadowPosition;
    fragTexCoord = texCoord;
    height = worldPosition.y;
}
