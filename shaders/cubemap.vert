#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragWorldPos;

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} push;

void main() {
    fragWorldPos = inPosition;
    // On projette le cube autour de la caméra
    gl_Position = push.proj * push.view * vec4(inPosition, 1.0);
}