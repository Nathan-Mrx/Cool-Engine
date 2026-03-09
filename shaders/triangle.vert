#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragNormal;

void main() {
    // 1. Échelle Centimétrique : On divise par 100 pour ramener les dimensions
    // (ex: rayon de 50cm) dans la petite boîte de l'écran Vulkan [-1, 1].
    vec3 scaledPos = inPosition * 0.01;

    // 2. Conversion Z-Up vers Vulkan :
    // - L'axe X reste X (Gauche / Droite)
    // - L'axe Z de ton moteur (Hauteur) devient l'axe Y de l'écran (inversé car Vulkan pointe vers le bas)
    // - L'axe Y de ton moteur (Profondeur) devient la profondeur Vulkan (+0.5 pour l'éloigner devant la caméra)
    gl_Position = vec4(
    scaledPos.x,
    -scaledPos.z,
    scaledPos.y + 0.5,
    1.0
    );

    fragNormal = inNormal;
}