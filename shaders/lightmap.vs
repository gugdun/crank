#version 330

// Input vertex attributes (raylib standard locations)
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec2 fragLightCoord;

void main() {
    fragTexCoord = vertexTexCoord;
    fragLightCoord = vertexTexCoord2;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
