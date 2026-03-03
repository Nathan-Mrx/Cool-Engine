#version 460
in vec3 vFragPos;
in vec3 vNormal;

// --- NOUVEAU : On spécifie 2 sorties (MRT) ---
layout(location = 0) out vec4 FragColor; // Va dans GL_COLOR_ATTACHMENT0
layout(location = 1) out int EntityID;   // Va dans GL_COLOR_ATTACHMENT1 (Texture R32I)

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;

// --- NOUVEAU : On reçoit l'ID de l'entité depuis le C++ ---
uniform int uEntityID;

void main() {
    // 1. Ambient
    vec3 ambient = uAmbientStrength * uLightColor;

    // 2. Diffuse
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uDiffuseStrength * uLightColor;

    vec3 result = (ambient + diffuse) * uColor;

    // --- SORTIES MULTIPLES ---
    FragColor = vec4(result, 1.0); // Écran normal
    EntityID = uEntityID;          // Texture cachée pour le clic
}