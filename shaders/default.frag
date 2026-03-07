#version 460 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoords;
in vec4 vFragPosLightSpace; // <-- NOUVEAU

layout(location = 0) out vec4 FragColor;
layout(location = 1) out int EntityID;

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform float uDiffuseStrength;

uniform int uEntityID;
uniform int uRenderMode;

uniform sampler2D uShadowMap; // <-- NOUVEAU : La texture de profondeur

// Tableau magique de Poisson
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

// --- LA FORMULE SECRÈTE DE JORGE JIMENEZ (Call of Duty / Activision) ---
// Génère un bruit parfait qui ne "scintille" pas quand la caméra bouge
float InterleavedGradientNoise(vec2 position_screen) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position_screen, magic.xy)));
}

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if(projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

    // 1. On récupère un angle aléatoire basé sur la position exacte du pixel à l'écran
    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    float angle = noise * 6.28318530718; // noise * 2 PI
    float s = sin(angle);
    float c = cos(angle);

    // 2. Matrice de rotation 2D
    mat2 rotationMat = mat2(c, -s, s, c);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);

    // J'ai réduit le spread de 2.5 à 1.5 pour un look beaucoup plus "net" (crisp)
    float spread = 1.5;

    for(int i = 0; i < 16; i++) {
        // 3. On fait pivoter le point de Poisson AVANT de lire la texture !
        vec2 rotatedOffset = rotationMat * poissonDisk[i];

        float pcfDepth = texture(uShadowMap, projCoords.xy + rotatedOffset * texelSize * spread).r;
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }

    shadow /= 16.0;

    return shadow;
}

void main() {
    // --- MODE UNLIT OU WIREFRAME ---
    if (uRenderMode == 1 || uRenderMode == 2) {
        FragColor = vec4(uColor, 1.0);
        EntityID = uEntityID;
        return;
    }

    // --- MODE LIT ---
    vec3 ambient = uAmbientStrength * uLightColor;

    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uDiffuseStrength * uLightColor;

    // CALCUL DE L'OMBRE (0.0 = Soleil, 1.0 = Ombre totale)
    float shadow = ShadowCalculation(vFragPosLightSpace, norm, lightDir);

    // L'ombre n'affecte QUE la lumière diffuse (et spéculaire plus tard), jamais l'ambiante !
    vec3 result = (ambient + (1.0 - shadow) * diffuse) * uColor;

    FragColor = vec4(result, 1.0);
    EntityID = uEntityID;
}