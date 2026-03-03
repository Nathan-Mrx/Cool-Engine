#version 460
in vec3 vFragPos;
in vec3 vNormal;

out vec4 FragColor;

uniform vec3 uColor;

// Paramètres de la Directional Light
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;

void main() {
    // 1. Ambient (Lumière indirecte de base)
    vec3 ambient = uAmbientStrength * uLightColor;

    // 2. Diffuse (Lumière directe de la source)
    vec3 norm = normalize(vNormal);
    // On inverse la direction car on veut calculer de la surface VERS la lumière
    vec3 lightDir = normalize(-uLightDir);

    // Produit scalaire (Dot product) pour savoir si la face regarde la lumière
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uDiffuseStrength * uLightColor;

    // Application sur la couleur de base de ton objet
    vec3 result = (ambient + diffuse) * uColor;
    FragColor = vec4(result, 1.0);
}