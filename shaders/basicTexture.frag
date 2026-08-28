#version 150

in vec2 fragTexCoord;
out vec4 outColor;

uniform sampler2D textureSampler;
uniform vec4 tint;
// 1 draws the sprite in the grey of remembered-but-unseen ground; 0 draws it normally.
uniform float desaturate;

void main(void)
{
    vec4 color = texture(textureSampler, fragTexCoord) * tint;
    float grey = dot(color.rgb, vec3(0.299, 0.587, 0.114)) * 0.7;
    outColor = vec4(mix(color.rgb, vec3(grey), desaturate), color.a);
}
