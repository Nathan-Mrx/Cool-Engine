#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragWorldPos;

// La structure contenant les matrices et les paramètres de l'atmosphère
layout(set = 0, binding = 1) uniform SkyboxUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection;

    float planetRadius;
    float atmosphereRadius;
    float rayleighScaleHeight;
    float mieScaleHeight;

    vec4 rayleighScattering;
    float mieScattering;

    vec3 padding;
} ubo;

void main() {
    fragWorldPos = inPosition;

    // On retire la translation de la caméra (pour que la skybox suive le joueur à l'infini)
    mat4 rotView = mat4(mat3(ubo.view));
    vec4 clipPos = ubo.proj * rotView * vec4(inPosition, 1.0);

    // Z = W pour que la profondeur soit maximale (1.0)
    gl_Position = clipPos.xyww;
}