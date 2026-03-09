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
layout(binding = 6) uniform sampler2D hdrMap;  // <--- On le garde temporairement pour avoir un reflet !
layout(binding = 7) uniform sampler2D brdfMap; // <--- Notre nouvelle LUT correctrice !

const float PI = 3.14159265359;
const vec2 invAtan = vec2(0.1591, 0.3183);

// La fonction reste là tant qu'on n'a pas de vraie Cubemap
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

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

    // 4. L'ÉQUATION PBR DIRECTE (Le Soleil)
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // ==========================================================
    // 5. IMAGE BASED LIGHTING (Avec la BRDF LUT !)
    // ==========================================================
    vec3 R = reflect(-V, N);
    vec2 envUV = SampleSphericalMap(normalize(R));
    vec3 envColor = texture(hdrMap, envUV).rgb;

    // La lecture de notre nouvelle texture magique :
    vec2 brdf = texture(brdfMap, vec2(max(dot(N, V), 0.0), roughness)).rg;

    vec3 specularIBL = envColor * (F0 * brdf.x + brdf.y) * (1.0 - roughness);
    vec3 diffuseIBL = envColor * albedo * 0.2;

    vec3 ambient = (diffuseIBL * kD + specularIBL) * ao;
    // ==========================================================

    vec3 color = ambient + Lo;

    // 6. TONEMAPPING ET GAMMA
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, albedoTex.a * ubo.baseColor.a);
}