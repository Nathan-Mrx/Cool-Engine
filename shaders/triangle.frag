#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

// --- NOTRE COLIS VULKAN EST LÀ ! ---
layout(binding = 0) uniform MaterialUBO {
    vec4 baseColor;
} ubo;

void main() {
    // Un petit éclairage basique pour donner du volume
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(fragNormal, lightDir), 0.2); // 0.2 = lumière ambiante

    // On applique la couleur envoyée par le C++ !
    outColor = vec4(ubo.baseColor.rgb * diff, ubo.baseColor.a);
}