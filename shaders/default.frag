#version 460 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoords;
in float vViewDepth; // <-- Distance de la caméra

layout(location = 0) out vec4 FragColor;
layout(location = 1) out int EntityID;

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;

uniform int uEntityID;
uniform int uRenderMode;

// =========================================================
// --- NOUVEAU : UNIFORMES POUR LE CSM ---
// =========================================================
uniform sampler2DArray uShadowMap; // Array de textures !
uniform mat4 uLightSpaceMatrices[3];
uniform float uCascadeDistances[3];

const vec2 poissonDisk[16] = vec2[](
vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ),
vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ),
vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ),
vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ),
vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100467 )
);

float InterleavedGradientNoise(vec2 position_screen) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position_screen, magic.xy)));
}

// Fonction qui retourne [Ombre, IndexDeLaCascade]
vec2 ShadowCalculation(vec3 fragPosWorld, vec3 normal, vec3 lightDir) {
    // 1. TROUVER LA BONNE CASCADE SELON LA DISTANCE
    int layer = -1;
    for (int i = 0; i < 3; ++i) {
        if (vViewDepth < uCascadeDistances[i]) {
            layer = i;
            break;
        }
    }
    if (layer == -1) layer = 2; // Sécurité

    // --- LE FIX NORMAL BIAS ---
    // Plus la cascade est grande, plus ses pixels sont gros.
    // On doit donc décaler virtuellement la surface plus fort pour fuir l'acné.
    float normalBiasOffset = 2.0; // 2 cm de décalage pour la cascade ultra-nette (Rouge)
    if (layer == 1) normalBiasOffset = 6.0;  // 6 cm pour la moyenne (Verte)
    if (layer == 2) normalBiasOffset = 20.0; // 20 cm pour la lointaine (Bleue)

    // On crée une fausse position légèrement surélevée
    vec3 biasedFragPos = fragPosWorld + normal * normalBiasOffset;

    // 2. PROJETER LE PIXEL (avec la position truquée !)
    vec4 fragPosLightSpace = uLightSpaceMatrices[layer] * vec4(biasedFragPos, 1.0);

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if(projCoords.z > 1.0) return vec2(0.0, layer);

    float currentDepth = projCoords.z;

    // On remet un tout petit depth bias de sécurité, mais le gros du travail est fait par le Normal Bias
    float bias = max(0.0005 * (1.0 - dot(normal, lightDir)), 0.00005);
    if (layer == 1) bias *= 1.5;
    if (layer == 2) bias *= 2.0;

    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    float angle = noise * 6.28318530718;
    float s = sin(angle);
    float c = cos(angle);
    mat2 rotationMat = mat2(c, -s, s, c);

    float shadow = 0.0;
    // L'astuce : textureSize sur un sampler2DArray retourne un vec3(width, height, layers) !
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float spread = 1.5;

    for(int i = 0; i < 16; i++) {
        vec2 rotatedOffset = rotationMat * poissonDisk[i];

        // --- NOUVEAU : On utilise un vec3 pour lire la texture. Le .z est l'index de la couche ! ---
        float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + rotatedOffset * texelSize * spread, float(layer))).r;

        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }

    shadow /= 16.0;

    return vec2(shadow, layer);
}

void main() {
    if (uRenderMode == 1 || uRenderMode == 2) {
        FragColor = vec4(uColor, 1.0);
        EntityID = uEntityID;
        return;
    }

    vec3 ambient = uAmbientStrength * uLightColor;
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uDiffuseStrength * uLightColor;

    // Récupération de l'ombre ET du niveau de la cascade utilisée
    vec2 shadowData = ShadowCalculation(vFragPos, norm, lightDir);
    float shadow = shadowData.x;
    float cascadeLayer = shadowData.y;

    vec3 result = (ambient + (1.0 - shadow) * diffuse) * uColor;

    // =========================================================
    // DEBUG CASCADES : DÉCOMMENTE CE BLOC POUR VOIR LES ZONES !
    // =========================================================
/*
    if (cascadeLayer == 0.0) result *= vec3(1.0, 0.5, 0.5); // Rouge : Très proche (0 à 15m)
    else if (cascadeLayer == 1.0) result *= vec3(0.5, 1.0, 0.5); // Vert : Moyen (15 à 50m)
    else if (cascadeLayer == 2.0) result *= vec3(0.5, 0.5, 1.0); // Bleu : Loin (50 à 150m)
*/

    FragColor = vec4(result, 1.0);
    EntityID = uEntityID;
}