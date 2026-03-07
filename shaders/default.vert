#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoords;

// --- NOUVEAU : On a besoin de la profondeur pour choisir la bonne cascade ---
out float vViewDepth;

void main() {
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoords = aTexCoords;

    // On calcule la position dans l'espace de la caméra (View Space)
    vec4 viewPos = uView * vec4(vFragPos, 1.0);

    // En OpenGL, la caméra regarde vers les Z négatifs, donc on inverse le signe
    vViewDepth = abs(viewPos.z);

    gl_Position = uProjection * viewPos;
}