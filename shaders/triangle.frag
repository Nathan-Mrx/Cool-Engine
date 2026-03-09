#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
    // Les normales vont de -1 à 1. On les convertit de 0 à 1 pour faire des couleurs RGB lisibles.
    outColor = vec4(fragNormal * 0.5 + 0.5, 1.0);
}