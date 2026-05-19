#version 330

in vec2 fragTexCoord;
in vec2 fragLightCoord;

// raylib binds MATERIAL_MAP_DIFFUSE to texture0 and MATERIAL_MAP_NORMAL/etc.
// We map our lightmap to texture1.
uniform sampler2D texture0;   // diffuse
uniform sampler2D texture1;   // lightmap atlas
uniform float lightScale;     // brightness multiplier (typically ~2.0)

out vec4 finalColor;

void main() {
    vec4 diffuse = texture(texture0, fragTexCoord);
    vec3 light = texture(texture1, fragLightCoord).rgb * lightScale;
    finalColor = vec4(diffuse.rgb * light, diffuse.a);
}
