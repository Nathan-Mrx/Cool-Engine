#version 450

// On récupère EXACTEMENT ce qu'on a configuré dans le vertexInputInfo !
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragNormal;

void main() {
    // On rétrécit un peu l'objet (0.5) car on n'a pas encore codé la caméra (Matrice de Projection) !
    gl_Position = vec4(inPosition * 0.5, 1.0);

    fragNormal = inNormal;
}