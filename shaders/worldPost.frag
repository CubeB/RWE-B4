#version 150

in vec2 fragTexCoord;
out vec4 outColor;

uniform sampler2D screenTexture;
uniform sampler2D dodgeMask;
// The options screen's gamma, as a plain exponent: 1.0 leaves the view
// untouched, higher lifts the midtones. The whole world passes through this
// blit, so this is the one place that catches everything the camera sees.
uniform float gamma;

void main(void)
{
    vec4 screenValue = texture(screenTexture, fragTexCoord);
    vec4 dodgeMaskValue = texture(dodgeMask, fragTexCoord);
    vec3 dodged = screenValue.rgb / (vec3(1.0, 1.0, 1.0) - dodgeMaskValue.rgb);
    outColor = vec4(pow(clamp(dodged, 0.0, 1.0), vec3(1.0 / gamma)), 1.0);
}
