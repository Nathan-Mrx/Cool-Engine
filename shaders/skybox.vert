#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragWorldPos;

// On récupère la vue et la projection via Push Constants
layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} push;

void main() {
    fragWorldPos = inPosition;

    // On retire la translation de la caméra (pour que la skybox suive le joueur à l'infini)
    mat4 rotView = mat4(mat3(push.view));
    vec4 clipPos = push.proj * rotView * vec4(inPosition, 1.0);

    // Z = W pour que la profondeur soit maximale (1.0)
    gl_Position = clipPos.xyww;
}