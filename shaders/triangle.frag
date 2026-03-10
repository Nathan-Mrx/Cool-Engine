#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;

layout(location = 0) out vec4 outColor;

// 1. LE BON UBO (Avec les variables CSM et DDGI !)
layout(binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    vec4 cameraPos;
    float metallic;
    float roughness;
    float ao;
    float padding;
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;

// --- DDGI ---
    vec4 ddgiStartPosition; // xyz = Position de départ, w = Espacement (Spacing)
    ivec4 ddgiProbeCount;   // xyz = Nombre de sondes, w = padding
} ubo;

// 2. LES TEXTURES DU MATÉRIAU
layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D metallicMap;
layout(binding = 4) uniform sampler2D roughnessMap;
layout(binding = 5) uniform sampler2D aoMap;

// 3. LES TEXTURES GLOBALES
layout(binding = 6) uniform samplerCube environmentMap;
layout(binding = 7) uniform sampler2D brdfMap;
layout(binding = 8) uniform samplerCube irradianceMap; // L'ancien statique (gardé en sécurité)
layout(binding = 9) uniform samplerCube prefilterMap;
layout(binding = 10) uniform sampler2DArray shadowMap;

// 4. LE NOUVEAU CÂBLE DDGI
layout(binding = 12) uniform sampler2D ddgiIrradianceMap;

const float PI = 3.14159265359;

// ==========================================================
// --- MATHÉMATIQUES DDGI : OCTAHEDRAL ENCODING ---
// ==========================================================
vec2 OctWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * mix(vec2(-1.0), vec2(1.0), step(vec2(0.0), v));
}

vec2 OctEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    return n.xy;
}

// --- MATHS CLASSIQUES PBR (Ton code existant) ---
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // --- LECTURE SÉCURISÉE DES TEXTURES ---
    vec3 albedo = texture(albedoMap, fragTexCoord).rgb * ubo.baseColor.rgb;
    float metallic = texture(metallicMap, fragTexCoord).r * ubo.metallic;
    float roughness = texture(roughnessMap, fragTexCoord).r * ubo.roughness;
    float aoTex = texture(aoMap, fragTexCoord).r;
    float finalAO = aoTex * ubo.ao;

    // --- CALCUL DE LA NORMALE (ANTI-NaN) ---
    vec3 N = normalize(fragNormal); // Géométrie pure de base

    // Dérivées des UV pour trouver la Tangente
    vec3 Q1  = dFdx(fragWorldPos);
    vec3 Q2  = dFdy(fragWorldPos);
    vec2 st1 = dFdx(fragTexCoord);
    vec2 st2 = dFdy(fragTexCoord);

    // On calcule le déterminant pour vérifier si les UV existent !
    float det = st1.s * st2.t - st2.s * st1.t;

    // S'il y a des UV valides (det différent de 0), on applique la Normal Map
    if (abs(det) > 0.00001) {
        vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;

        vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
        vec3 B = normalize(-Q1 * st2.s + Q2 * st1.s);
        mat3 TBN = mat3(T, B, N);

        N = normalize(TBN * tangentNormal);
    }

    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);

    vec3 lightDir = normalize(vec3(0.5, 0.5, 1.0));
    vec3 L = lightDir;
    vec3 H = normalize(V + L);

    // Ombres (Simplifié ici pour faire de la place)
    float shadow = 1.0;

    vec3 radiance = vec3(5.0) * shadow;
    float NdotL = max(dot(N, L), 0.0);

    if (NdotL > 0.0) {
        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3 F_dir = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator    = NDF * G * F_dir;
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kS_dir = F_dir;
        vec3 kD_dir = vec3(1.0) - kS_dir;
        kD_dir *= 1.0 - metallic;

        Lo = (kD_dir * albedo / PI + specular) * radiance * NdotL;
    }

    // ==========================================================
    // 5. IMAGE BASED LIGHTING & DDGI
    // ==========================================================
    vec3 F_ibl = fresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = vec3(1.0) - kS_ibl;
    kD_ibl *= 1.0 - metallic;

    vec3 ddgiIrradiance = vec3(0.0);

    // SÉCURITÉ ANTI-CRASH : On vérifie que le DDGI est bien actif avant de diviser !
    if (ubo.ddgiProbeCount.x > 0 && ubo.ddgiStartPosition.w > 0.0) {
        // --- L'INTERPOLATION DES 8 SONDES ---
        vec3 gridCoord = (fragWorldPos - ubo.ddgiStartPosition.xyz) / ubo.ddgiStartPosition.w;
        ivec3 baseProbeCoords = ivec3(floor(gridCoord));
        vec3 alpha = fract(gridCoord);

        float weightSum = 0.0;
        int probesPerRow = ubo.ddgiProbeCount.x * ubo.ddgiProbeCount.y;

        // On boucle sur les 8 sommets du cube de sondes autour de nous
        for (int i = 0; i < 8; ++i) {
            ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
            ivec3 probeCoord = clamp(baseProbeCoords + offset, ivec3(0), ubo.ddgiProbeCount.xyz - 1);

            int probeIndex = probeCoord.x + probeCoord.y * ubo.ddgiProbeCount.x + probeCoord.z * probesPerRow;
            int gridX = probeIndex % probesPerRow;
            int gridY = probeIndex / probesPerRow;

            vec2 octUV = OctEncode(N);
            octUV = octUV * 0.5 + 0.5;

            float texWidth = float(probesPerRow * 8);
            float texHeight = float(ubo.ddgiProbeCount.z * 8);

            vec2 probePixelPos = vec2(gridX * 8.0, gridY * 8.0);
            vec2 uv = (probePixelPos + 1.0 + octUV * 6.0) / vec2(texWidth, texHeight);

            vec3 trilinear = mix(1.0 - alpha, alpha, vec3(offset));
            float weight = trilinear.x * trilinear.y * trilinear.z;

            vec3 probeLight = texture(ddgiIrradianceMap, uv).rgb;

            ddgiIrradiance += probeLight * weight;
            weightSum += weight;
        }
        ddgiIrradiance /= max(weightSum, 0.001);
    }
    else {
        // FALLBACK : Si le volume DDGI n'existe pas (Preview Material), on utilise le cubemap statique
        ddgiIrradiance = texture(irradianceMap, N).rgb;
    }

    // On applique la lumière Globale
    float iblIntensity = 2.0;
    vec3 diffuseIBL = ddgiIrradiance * albedo * iblIntensity;

    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 envColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb * iblIntensity;
    vec2 brdf = texture(brdfMap, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = envColor * (F_ibl * brdf.x + brdf.y);

    vec3 ambient = (kD_ibl * diffuseIBL + specularIBL) * finalAO;

    vec3 color = Lo + ambient;

    // HDR Tonemapping et Gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, ubo.baseColor.a);
}