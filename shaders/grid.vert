#version 460
layout(location = 0) in vec3 aPos;

uniform mat4 uProjection;
uniform mat4 uView;

out vec3 vWorldPos;

void main() {
    // aPos est un quad de 1x1. On le multiplie par 100 000 cm (1000 mètres)
    vWorldPos = aPos * 100000.0;
    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}