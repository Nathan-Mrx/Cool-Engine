#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragNormal;

void main() {
    // --- CONVERSION Z-UP VERS VULKAN ---
    // L'axe X (Gauche/Droite) reste l'axe X
    // L'axe Z (Haut/Bas) devient l'axe Y pour l'écran (inversé car Vulkan Y pointe vers le bas)
    // L'axe Y (Avant/Arrière) devient l'axe Z pour la profondeur Vulkan (0.0 à 1.0)

    gl_Position = vec4(
    inPosition.x * 0.5,
    -inPosition.z * 0.5,          // L'axe Z de ton moteur gère la hauteur de l'écran !
    inPosition.y * 0.5 + 0.5,     // L'axe Y de ton moteur gère la profondeur !
    1.0
    );

    fragNormal = inNormal;
}