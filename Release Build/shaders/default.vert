#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 color;

uniform float scale;
uniform vec2 offset;

void main() {
    gl_Position = vec4(
        aPos.x * scale + offset.x,
        aPos.y * scale + offset.y,
        aPos.z,
        1.0
    );
    color = aColor;
}