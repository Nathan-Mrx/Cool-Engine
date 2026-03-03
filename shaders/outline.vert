#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uOutlineWidth;

void main() {
    // On pousse le sommet le long de sa normale
    vec3 extrudedPos = aPos + (aNormal * uOutlineWidth);
    gl_Position = uProjection * uView * uModel * vec4(extrudedPos, 1.0);
}