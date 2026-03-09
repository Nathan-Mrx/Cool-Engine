#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragNormal;

// --- NOUVEAU : On récupère la matrice du CPU ! ---
layout(push_constant) uniform PushConstants {
    mat4 mvp;
} push;

void main() {
    // La matrice de projection s'occupe de la taille, de la caméra et de l'étirement !
    gl_Position = push.mvp * vec4(inPosition, 1.0);

    fragNormal = inNormal;
}