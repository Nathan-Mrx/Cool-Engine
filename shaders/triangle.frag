#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    vec4 cameraPos;
    float metallic;
    float roughness;
    float ao;
    float padding;
} ubo;

layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D metallicMap;
layout(binding = 4) uniform sampler2D roughnessMap;
layout(binding = 5) uniform sampler2D aoMap;

layout(binding = 6) uniform samplerCube environmentMap;
layout(binding = 7) uniform sampler2D brdfMap;
layout(binding = 8) uniform samplerCube irradianceMap;
layout(binding = 9) uniform samplerCube prefilterMap;

const float PI = 3.14159265359;

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
    // 4. L'ÉQUATION PBR DIRECTE (Le Soleil uniquement !)
    // ==========================================================
    vec3 Lo = vec3(0.0);
    float NdotL = max(dot(N, L), 0.0);

    if (NdotL > 0.0) { // On éclaire que si la face regarde le soleil !
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
    // 5. IMAGE BASED LIGHTING (L'Ambiance indépendante !)
    // ==========================================================

    // Le Fresnel Ambiant (Il utilise N et V, pas le soleil !)
    vec3 F_ibl = fresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = vec3(1.0) - kS_ibl;
    kD_ibl *= 1.0 - metallic;

    // Multiplicateur pour booster le ciel si le HDR est trop terne
    float iblIntensity = 2.0;

    // Lumière diffuse ambiante
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo * iblIntensity;

    // Lumière spéculaire (Reflets PBR avec Roughness !)
    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0; // Nos 5 niveaux (0 à 4)
    vec3 envColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb * iblIntensity;

    vec2 brdf = texture(brdfMap, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = envColor * (F_ibl * brdf.x + brdf.y);

    // L'Ambiance n'est plus jamais détruite par le soleil !
    vec3 ambient = (kD_ibl * diffuseIBL + specularIBL) * ao;

    // ==========================================================

    vec3 color = ambient + Lo;

    // 6. TONEMAPPING ET GAMMA
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, albedoTex.a * ubo.baseColor.a);
}