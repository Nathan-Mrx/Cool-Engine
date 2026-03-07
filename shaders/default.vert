#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// --- NOUVEAU : La matrice du "Soleil" ---
uniform mat4 uLightSpaceMatrix;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoords;
out vec4 vFragPosLightSpace; // <-- NOUVEAU : Position pour l'ombre

void main() {
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoords = aTexCoords;

    // On projette ce sommet dans la "caméra" de la lumière
    vFragPosLightSpace = uLightSpaceMatrix * vec4(vFragPos, 1.0);

    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}