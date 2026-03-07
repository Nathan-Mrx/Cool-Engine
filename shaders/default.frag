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

// --- LA SPIRALE DE VOGEL ---
const float GOLDEN_ANGLE = 2.39996323; // PI * (3.0 - sqrt(5.0))

vec2 VogelDiskSample(int i, int numSamples, float noiseAngle) {
    // Calcule le rayon (plus on avance, plus on s'éloigne du centre)
    float r = sqrt(float(i) + 0.5) / sqrt(float(numSamples));
    // Calcule l'angle (Nombre d'or + notre bruit rotatif)
    float theta = float(i) * GOLDEN_ANGLE + noiseAngle;

    return vec2(r * cos(theta), r * sin(theta));
}

float InterleavedGradientNoise(vec2 position_screen) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position_screen, magic.xy)));
}

// Fonction qui retourne [Ombre, IndexDeLaCascade]
vec2 ShadowCalculation(vec3 fragPosWorld, vec3 normal, vec3 lightDir) {
    // ------------------------------------------------------------------
    // ROUTINE CSM (Identique à avant)
    // ------------------------------------------------------------------
    int layer = -1;
    for (int i = 0; i < 3; ++i) {
        if (vViewDepth < uCascadeDistances[i]) { layer = i; break; }
    }
    if (layer == -1) layer = 2;

    float normalBiasOffset = 2.0;
    if (layer == 1) normalBiasOffset = 6.0;
    if (layer == 2) normalBiasOffset = 20.0;

    vec3 biasedFragPos = fragPosWorld + normal * normalBiasOffset;
    vec4 fragPosLightSpace = uLightSpaceMatrices[layer] * vec4(biasedFragPos, 1.0);

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if(projCoords.z > 1.0) return vec2(0.0, layer);

    float currentDepth = projCoords.z;
    float bias = max(0.0005 * (1.0 - dot(normal, lightDir)), 0.00005);

    // Bruit rotatif pour casser les motifs (utilisé deux fois maintenant !)
    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    float angle = noise * 6.28318530718;
    float s = sin(angle);
    float c = cos(angle);
    mat2 rotationMat = mat2(c, -s, s, c);

    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));

    // ==================================================================
    // PCSS - ÉTAPE 1 : BLOCKER SEARCH
    // ==================================================================
    int blockers = 0;
    float avgBlockerDepth = 0.0;
    float searchRadius = 4.0;
    if (layer == 1) searchRadius = 2.0;
    if (layer == 2) searchRadius = 1.0;

    int blockerSamples = 16; // 16 est suffisant pour la recherche
    for(int i = 0; i < blockerSamples; i++) {
        // NOUVEAU : On appelle la fonction Vogel
        vec2 offset = VogelDiskSample(i, blockerSamples, angle) * searchRadius * texelSize;
        float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + offset, float(layer))).r;

        if (pcfDepth < currentDepth - bias) {
            blockers++;
            avgBlockerDepth += pcfDepth;
        }
    }

    // Si rien ne bloque la lumière dans les environs, on est en plein soleil !
    if (blockers == 0) return vec2(0.0, layer);

    avgBlockerDepth /= float(blockers); // Profondeur moyenne de l'obstacle

    // ==================================================================
    // PCSS - ÉTAPE 2 : PENUMBRA ESTIMATION (Calcul du flou de pénombre)
    // ==================================================================
    // Distance entre la surface actuelle et l'objet qui projette l'ombre
    float distanceToBlocker = currentDepth - avgBlockerDepth;

    // LA TAILLE DU SOLEIL : C'est LA variable avec laquelle tu vas jouer.
    // Plus elle est grande, plus les ombres lointaines seront floues.
    float sunSize = 350.0;

    float penumbraRadius = distanceToBlocker * sunSize;

    // On clampe pour éviter que le flou ne détruise les performances ou bave trop
    float maxBlur = 12.0;
    if (layer == 1) maxBlur = 6.0;
    if (layer == 2) maxBlur = 2.0;

    // Le rayon final qui va servir à écarter nos points de lecture
    float filterRadiusUV = clamp(penumbraRadius, 1.0, maxBlur);

    // ==================================================================
    // PCSS - ÉTAPE 3 : VARIABLE PCF
    // ==================================================================
    float shadow = 0.0;
    int pcfSamples = 32; // <-- LA MAGIE EST ICI : Essaie 32, ou 64 si ton GPU encaisse bien !

    for(int i = 0; i < pcfSamples; i++) {
        // NOUVEAU : On utilise Vogel pour générer 32 points floutés
        vec2 offset = VogelDiskSample(i, pcfSamples, angle) * filterRadiusUV * texelSize;
        float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + offset, float(layer))).r;
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }

    shadow /= float(pcfSamples);
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