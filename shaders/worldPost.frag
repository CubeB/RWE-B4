#version 150

in vec2 fragTexCoord;
out vec4 outColor;

uniform sampler2D screenTexture;
uniform sampler2D dodgeMask;
// Coverage of the buildings, 1 inside and 0 outside, drawn at the supersampled
// size and read back here through the same linear filter that does the
// downsample. See the halo comment below.
uniform sampler2D buildingMask;
// The options screen's gamma. The original is not a gamma curve at all: it
// rewrites the palette as min(255, c * m) with m = 0.5 + v/24 over a slider
// of 20 steps, so it runs 0.5x to 1.333x and 1.0 is untouched. A straight
// multiply is therefore the faithful thing, and this blit is the one place
// the whole world view passes through.
uniform float gamma;

// The purple halo, reproduced on purpose.
//
// It is a bug, and it is the original's. Jon Mavor, who wrote the engine:
// "Ever notice that a lot of the buildings have a weird purple halo? Basically
// the table broke when dealing with the edge and transparency because I didn't
// have a correct way to represent that." He anti-aliased the non-animating part
// of a building by rendering it into a buffer of twice the size in each
// dimension and box-filtering it down through a lookup table -- "a lookup on
// the top two pixel and the bottom two pixels. The results from those two ops
// were then looked up to give me the final color, so 3 lookups." At the
// silhouette the pairs being averaged are a real colour and whatever stood for
// transparent, and the answer that came back was wrong.
//
// What stood for transparent is measurable rather than guessable. Blending
// every palette entry against each of the 256 possible partners through the
// shipped PALETTE.ALP and asking which partner turns ordinary building colours
// purple gives one clear winner: index 253, which is (255, 0, 255) -- plain
// magenta, the usual colour key. Nothing else is close; the runner-up is index
// 5, (128, 0, 128), and the composited bitmap's own transparent index 1 comes
// back reddish instead. Averaged with magenta and snapped to the nearest entry
// the palette actually has, a mid grey lands on (123, 59, 71) and white on
// (175, 111, 127). The mean over the row is the colour below.
//
// RWE takes the row's mean rather than the per-entry value, because by this
// point in the frame the pixel is a blended colour and its palette index is
// long gone. Width and strength are settings because the artefact does not
// survive translation on its own: the original's halo is about one pixel of a
// 640x480 screen, and one pixel of a modern one is a quarter of the size, so
// left alone it would be technically faithful and very subtle indeed, which is
// why the defaults are 3 and 75 rather than 1 and something smaller.
//
// Beware the trap those two numbers sit next to. The halo was twice reported
// invisible and twice the width was blamed, because "one pixel of a 640x480
// screen is nothing here" is a ready explanation and it fits. It was wrong
// both times: the coverage mask was empty, because the pass that fills it
// redraws geometry the world pass has already drawn and was running under
// GL_LESS, which rejects a fragment sitting at exactly the stored depth. The
// numbers here were never the reason nothing showed up. See the halo mask pass
// in GameScene_render.cpp for the fix and the reasoning.
//
// One artefact is inherent to doing this from a screen-space mask and is worth
// knowing about before it is reported as a bug. The original anti-aliases each
// building's bitmap in isolation, so its halo follows its own outline and
// whatever stands in front is painted over the top. Here the mask is drawn
// depth-tested against the finished world, so the coverage also ends wherever
// something else occludes the building -- and a boundary is a boundary, so a
// tank parked in front of a factory picks up a thread of halo along the edge
// that overlaps it. Dropping the depth test trades that for the worse one: a
// building completely hidden behind a hill would draw its outline over the
// hill.
uniform vec3 haloColor;
// How far from the outline the halo reaches, in output pixels.
uniform float haloWidth;
// 0 leaves it out entirely.
uniform float haloStrength;
uniform vec2 haloTexelStep;

// How much of an outline is near this pixel: the spread of the coverage over a
// small ring, which is zero well inside the building and well outside it and
// rises to 1 across the edge. Reaching for the spread rather than for the
// single sample is what lets the width be more than the one pixel the
// downsample would give on its own.
float outlineNearness()
{
    float here = texture(buildingMask, fragTexCoord).r;
    float low = here;
    float high = here;

    vec2 reach = haloTexelStep * haloWidth;
    for (int i = 0; i < 4; ++i)
    {
        vec2 offset = (i == 0) ? vec2(reach.x, 0.0)
            : (i == 1) ? vec2(-reach.x, 0.0)
            : (i == 2) ? vec2(0.0, reach.y)
            : vec2(0.0, -reach.y);
        float neighbour = texture(buildingMask, fragTexCoord + offset).r;
        low = min(low, neighbour);
        high = max(high, neighbour);
    }

    return high - low;
}

void main(void)
{
    vec4 screenValue = texture(screenTexture, fragTexCoord);
    vec4 dodgeMaskValue = texture(dodgeMask, fragTexCoord);
    vec3 dodged = screenValue.rgb / (vec3(1.0, 1.0, 1.0) - dodgeMaskValue.rgb);

    if (haloStrength > 0.0)
    {
        dodged = mix(dodged, haloColor, outlineNearness() * haloStrength);
    }

    outColor = vec4(clamp(dodged * gamma, 0.0, 1.0), 1.0);
}
