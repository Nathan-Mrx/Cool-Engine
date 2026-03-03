#version 460
in vec3 vFragPos;
in vec3 vNormal;

layout(location = 0) out vec4 FragColor; // Va dans GL_COLOR_ATTACHMENT0
layout(location = 1) out int EntityID;   // Va dans GL_COLOR_ATTACHMENT1 (Texture R32I)

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;


uniform int uEntityID;
uniform int uRenderMode;

void main() {
    // --- MODE UNLIT OU WIREFRAME ---
    if (uRenderMode == 1 || uRenderMode == 2) {
        FragColor = vec4(uColor, 1.0); // Couleur pure, sans ombre !
        EntityID = uEntityID;
        return; // On arrête le shader ici
    }

    // --- MODE LIT (Ton code existant) ---
    vec3 ambient = uAmbientStrength * uLightColor;
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uDiffuseStrength * uLightColor;

    vec3 result = (ambient + diffuse) * uColor;

    FragColor = vec4(result, 1.0);
    EntityID = uEntityID;
}