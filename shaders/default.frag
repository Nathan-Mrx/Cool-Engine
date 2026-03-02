#version 460 core
out vec4 FragColor;
uniform vec3 uColor; // On pourra changer cette couleur via C++ !

void main() {
    FragColor = vec4(uColor, 1.0f);
}