#version 450

layout(location = 0) in vec3 inPosition;

// La matrice combinée (Projection * Vue) de notre "Caméra Soleil"
layout(push_constant) uniform PushConstants {
    mat4 mvp;
} push;

void main() {
    gl_Position = push.mvp * vec4(inPosition, 1.0);
}