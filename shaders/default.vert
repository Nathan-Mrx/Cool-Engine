#version 460
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal; // Ton Mesh.cpp envoie déjà les normales ici !
layout(location = 2) in vec2 aTexCoords;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vFragPos;
out vec3 vNormal;

void main() {
    vFragPos = vec3(uModel * vec4(aPos, 1.0));

    // Matrice normale : indispensable si on modifie le Scale de l'objet,
    // sinon les normales sont déformées et la lumière bug.
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;

    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}