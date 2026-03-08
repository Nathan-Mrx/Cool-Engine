#version 450

// On code les 3 points du triangle directement dans le GPU !
vec2 positions[3] = vec2[](
vec2(0.0, -0.5),
vec2(0.5, 0.5),
vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
vec3(1.0, 0.0, 0.0), // Rouge
vec3(0.0, 1.0, 0.0), // Vert
vec3(0.0, 0.0, 1.0)  // Bleu
);

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}