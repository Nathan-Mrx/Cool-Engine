#version 460
in vec3 vWorldPos;

// --- SORTIES MULTIPLES ---
layout(location = 0) out vec4 FragColor;
layout(location = 1) out int EntityID;

uniform vec3 uCameraPos;

void main() {
    float gridSize = 100.0; // 100 unités = 1 mètre

    // Dérivation mathématique pour des lignes anti-aliasées parfaites
    vec2 coord = vWorldPos.xy / gridSize;
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = min(grid.x, grid.y);
    vec4 color = vec4(0.4, 0.4, 0.4, 1.0 - min(line, 1.0));

    // Axes de couleur façon Unreal/Godot (Axe X en rouge, Axe Y en vert)
    if (abs(vWorldPos.y) < 2.0) color.rgb = vec3(0.8, 0.2, 0.2); // X rouge
    if (abs(vWorldPos.x) < 2.0) color.rgb = vec3(0.2, 0.8, 0.2); // Y vert

    // Fading : la grille disparaît doucement dans le brouillard après 50 mètres
    float distanceToCam = length(uCameraPos.xy - vWorldPos.xy);
    float fade = 1.0 - smoothstep(1000.0, 5000.0, distanceToCam);

    color.a *= fade;

    // On ne dessine pas les pixels invisibles
    if(color.a < 0.01) discard;

    FragColor = color;
    EntityID = -1; // -1 signifie qu'on a cliqué dans le vide (pas sur une entité)
}