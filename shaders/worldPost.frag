#version 150

in vec2 fragTexCoord;
out vec4 outColor;

uniform sampler2D screenTexture;
uniform sampler2D dodgeMask;
// The options screen's gamma. The original is not a gamma curve at all: it
// rewrites the palette as min(255, c * m) with m = 0.5 + v/24 over a slider
// of 20 steps, so it runs 0.5x to 1.333x and 1.0 is untouched. A straight
// multiply is therefore the faithful thing, and this blit is the one place
// the whole world view passes through.
uniform float gamma;

void main(void)
{
    vec4 screenValue = texture(screenTexture, fragTexCoord);
    vec4 dodgeMaskValue = texture(dodgeMask, fragTexCoord);
    vec3 dodged = screenValue.rgb / (vec3(1.0, 1.0, 1.0) - dodgeMaskValue.rgb);
    outColor = vec4(clamp(dodged * gamma, 0.0, 1.0), 1.0);
}
