#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;

layout(location = 0) out vec4 outColor;

// 1. LE BON UBO (Avec les variables CSM !)
layout(binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    vec4 cameraPos;
    float metallic;
    float roughness;
    float ao;
    float padding;
    mat4 lightSpaceMatrices[4]; // Nos 4 matrices solaires
    vec4 cascadeSplits;         // Nos 4 distances
} ubo;

// 2. LES TEXTURES DU MATÉRIAU
layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D metallicMap;
layout(binding = 4) uniform sampler2D roughnessMap;
layout(binding = 5) uniform sampler2D aoMap;

// 3. LES TEXTURES GLOBALES (IBL + Ombres)
layout(binding = 6) uniform samplerCube environmentMap;
layout(binding = 7) uniform sampler2D brdfMap;
layout(binding = 8) uniform samplerCube irradianceMap;
layout(binding = 9) uniform samplerCube prefilterMap;
layout(binding = 10) uniform sampler2DArray shadowMap;

const float PI = 3.14159265359;

// --- MATHS DE LA LUMIÈRE (Cook-Torrance) ---
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
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// --- CASCADED SHADOW MAPS ---
float ShadowCalculation(vec3 fragWorldPos, vec3 N, vec3 L) {
    // Distance exacte de la caméra au pixel
    float depth = length(ubo.cameraPos.xyz - fragWorldPos);

    // On cherche dans quelle tranche on se trouve
    int cascadeIndex = 0;
    for(int i = 0; i < 3; ++i) {
        if(depth > ubo.cascadeSplits[i]) {
            cascadeIndex = i + 1;
        }
    }

    // Projection depuis les yeux du soleil pour CETTE cascade
    vec4 fragPosLightSpace = ubo.lightSpaceMatrices[cascadeIndex] * vec4(fragWorldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Si on regarde au-delà du frustum du soleil
    if(projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;

    // Biais anti-acné progressif (plus la cascade est grande, plus l'erreur d'arrondi est forte)
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
    if (cascadeIndex > 0) bias *= 2.0;
    if (cascadeIndex > 1) bias *= 2.0;

    // PCF (Floutage) sur un tableau 2D (sampler2DArray utilise un vec3)
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0).xy;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, cascadeIndex)).r;
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    // 1. LECTURE DES TEXTURES
    vec4 albedoTex = texture(albedoMap, fragTexCoord);
    vec3 albedo = pow(albedoTex.rgb, vec3(2.2)) * ubo.baseColor.rgb;
    float metallic = texture(metallicMap, fragTexCoord).r * ubo.metallic;
    float roughness = texture(roughnessMap, fragTexCoord).r * ubo.roughness;
    float ao = texture(aoMap, fragTexCoord).r * ubo.ao;

    // 2. NORMAL MAPPING
    vec3 N_tex = texture(normalMap, fragTexCoord).rgb;
    N_tex = N_tex * 2.0 - 1.0;
    vec3 N_geom = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    T = normalize(T - dot(T, N_geom) * N_geom);
    vec3 B = cross(N_geom, T);
    mat3 TBN = mat3(T, B, N_geom);
    vec3 N = normalize(TBN * N_tex);

    // 3. CAMÉRA ET SOLEIL
    vec3 camPos = ubo.cameraPos.xyz;
    vec3 V = normalize(camPos - fragWorldPos);

    vec3 lightDir = normalize(vec3(0.5, 0.5, 1.0));
    vec3 lightColor = vec3(3.0);
    vec3 L = lightDir;
    vec3 H = normalize(V + L);
    vec3 radiance = lightColor;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // ==========================================================
    // 4. L'ÉQUATION PBR DIRECTE (Le Soleil)
    // ==========================================================
    vec3 Lo = vec3(0.0);
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
    // 5. IMAGE BASED LIGHTING (L'Ambiance)
    // ==========================================================

    vec3 F_ibl = fresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = vec3(1.0) - kS_ibl;
    kD_ibl *= 1.0 - metallic;

    float iblIntensity = 2.0;

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo * iblIntensity;

    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 envColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb * iblIntensity;

    vec2 brdf = texture(brdfMap, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = envColor * (F_ibl * brdf.x + brdf.y);

    vec3 ambient = (kD_ibl * diffuseIBL + specularIBL) * ao;

    // ==========================================================
    // 6. OMBRES (CSM)
    // ==========================================================

    float shadow = ShadowCalculation(fragWorldPos, N, L);
    vec3 color = ambient + (1.0 - shadow) * Lo;

    // 7. TONEMAPPING ET GAMMA
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, albedoTex.a * ubo.baseColor.a);
}