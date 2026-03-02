#version 460 core
layout (location = 0) in vec3 aPos;

// On récupère nos 3 matrices depuis le C++
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    // La multiplication se fait de droite à gauche !
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}