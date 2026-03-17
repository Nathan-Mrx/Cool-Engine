#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent; // <--- AJOUT : On récupère la tangente du Mesh

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent; // <--- AJOUT : On l'envoie au Fragment

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} push;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = push.viewProj * worldPos;

    fragWorldPos = worldPos.xyz;

    // On tourne la Normale ET la Tangente avec l'objet
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent = normalize(normalMatrix * inTangent); // <--- AJOUT

    fragTexCoord = inTexCoord;
}