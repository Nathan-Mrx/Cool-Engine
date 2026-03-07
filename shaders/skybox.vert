#version 460 core
layout (location = 0) in vec3 aPos;

uniform mat4 uProjection;
uniform mat4 uView;

out vec3 vWorldPos;

void main() {
    vWorldPos = aPos;

    // On convertit la matrice 4x4 en 3x3 pour supprimer la translation.
    // Ainsi, la Skybox suit la caméra et le joueur ne l'atteindra jamais !
    mat4 rotView = mat4(mat3(uView));
    vec4 clipPos = uProjection * rotView * vec4(vWorldPos, 1.0);

    // ASTUCE MAGIQUE : En forçant le Z à égaler le W, la profondeur de la skybox
    // sera toujours exactement de 1.0 (le maximum). Elle sera donc toujours au fond !
    gl_Position = clipPos.xyww;
}