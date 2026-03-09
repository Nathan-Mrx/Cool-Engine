#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord; // Tes coordonnées UV (vérifie que ton .vert les envoie bien ici !)

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform MaterialUBO {
    vec4 baseColor;
} ubo;

// --- NOUVEAU : NOTRE TEXTURE EST LÀ ---
layout(binding = 1) uniform sampler2D texSampler;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(fragNormal, lightDir), 0.2); // Éclairage basique

    // On lit le pixel de l'image
    vec4 textureColor = texture(texSampler, fragTexCoord);

    // On multiplie la couleur de base * la texture * la lumière
    outColor = vec4(ubo.baseColor.rgb * textureColor.rgb * diff, ubo.baseColor.a * textureColor.a);
}