#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

// Le nouveau Push Constant avec les DEUX matrices !
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} push;

void main() {
    // Calcul de la position exacte de l'objet dans le monde 3D
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = push.viewProj * worldPos;

    // Transmission des données au Fragment Shader
    fragWorldPos = worldPos.xyz;

    // On corrige la normale selon la rotation de l'objet (Matrice inverse transposée)
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragTexCoord = inTexCoord;
}