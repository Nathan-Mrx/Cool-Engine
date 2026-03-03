#version 460 core

// --- SORTIES MULTIPLES ---
layout(location = 0) out vec4 FragColor;
layout(location = 1) out int EntityID;

uniform vec3 uColor;

void main() {
    FragColor = vec4(uColor, 1.0);
    EntityID = -1; // Ne pas sélectionner l'outline si on clique dessus
}