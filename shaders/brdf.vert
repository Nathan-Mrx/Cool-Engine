#version 450
layout(location = 0) out vec2 outUV;

void main() {
    // Astuce Vulkan : gl_VertexIndex génère un triangle qui couvre l'écran (-1 à 1)
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}