#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// Notre UBO avec les nouveaux paramètres PBR
layout(binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
} ubo;

// Les 5 textures du colis PBR !
layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D metallicMap;
layout(binding = 4) uniform sampler2D roughnessMap;
layout(binding = 5) uniform sampler2D aoMap;

void main() {
    // 1. Lecture de la texture principale
    vec4 albedoTex = texture(albedoMap, fragTexCoord);
    vec3 albedo = albedoTex.rgb * ubo.baseColor.rgb;

    // 2. Éclairage PBR directionnel (basique pour l'instant)
    vec3 lightPos = vec3(500.0, 1000.0, 500.0);
    vec3 lightColor = vec3(1.0, 1.0, 1.0) * 1.5;

    vec3 N = normalize(fragNormal); // Bientôt on utilisera la Normal Map !
    vec3 L = normalize(lightPos - fragWorldPos);

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * albedo * lightColor;

    // 3. Lumière ambiante très basique
    vec3 ambient = vec3(0.1) * albedo;

    outColor = vec4(ambient + diffuse, albedoTex.a * ubo.baseColor.a);
}