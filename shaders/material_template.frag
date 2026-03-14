#version 450
// VULKAN

// =========================================================================================
// PIPELINE INPUTS (Vertex -> Fragment)
// =========================================================================================
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;

// =========================================================================================
// PIPELINE OUTPUTS
// =========================================================================================
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBrightColor;

// =========================================================================================
// STANDARD UNIFORMS (UBO)
// =========================================================================================
layout(binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    vec4 cameraPos;
    float metallic;
    float roughness;
    float ao;
    float padding;
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    vec4 ddgiStartPosition;
    ivec4 ddgiProbeCount;
    vec4 lightDirection;  // xyz = direction TO light, w = unused
    vec4 lightColor;      // xyz = color * intensity, w = ambient intensity
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
layout(binding = 10) uniform sampler2DArray shadowMap;
layout(binding = 12) uniform sampler2D ddgiIrradianceMap;

// =========================================================================================
// @INSERT_MATERIAL_UNIFORMS@
// =========================================================================================

// --- CONSTANTS ---
const float PI = 3.14159265359;

// =========================================================================================
// MATH HELPERS
// =========================================================================================
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
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

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// TBN Matrix calculation for normal mapping
vec3 getNormalFromMap(vec3 normalMapColor) {
    vec3 tangentNormal = normalMapColor * 2.0 - 1.0;
    vec3 N = normalize(fragNormal);
    vec3 T;

    if (length(fragTangent) > 0.1) {
        T = normalize(fragTangent);
    } else {
        // Fallback pour mesh sans tangentes
        vec3 Q1 = dFdx(fragWorldPos);
        vec3 Q2 = dFdy(fragWorldPos);
        vec2 st1 = dFdx(fragTexCoord);
        vec2 st2 = dFdy(fragTexCoord);
        float det = (st1.x * st2.y - st2.x * st1.y);
        if (abs(det) > 0.0001) {
            T = normalize((Q1 * st2.y - Q2 * st1.y) / det);
        } else {
            T = cross(abs(N.z) > 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0), N);
        }
    }

    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

// =========================================================================================
// SHADOW MAPPING
// =========================================================================================
// Shadow mapping is currently omitted in custom materials to keep them fast.
// TODO: Port the Vogel PCSS properly.

// =========================================================================================
// MAIN ENTRY POINT
// =========================================================================================
void main() {
    // --------------------------------------------------------------------------
    // DEFAULT MATERIAL PROPERTIES
    // --------------------------------------------------------------------------
    vec3  Albedo    = ubo.baseColor.rgb;
    vec3  Normal    = fragNormal;
    float Metallic  = ubo.metallic;
    float Roughness = ubo.roughness;
    float AO        = ubo.ao;
    vec3  Emissive  = vec3(0.0);

    // --------------------------------------------------------------------------
    // @INSERT_MATERIAL_LOGIC@
    // --------------------------------------------------------------------------

    // --------------------------------------------------------------------------
    // PBR LIGHTING CALCULATION
    // --------------------------------------------------------------------------
    vec3 N = normalize(Normal);
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, Albedo, Metallic);

    vec3 Lo = vec3(0.0);

    // --- DIRECTIONAL SUN LIGHT ---
    vec3 L = normalize(ubo.lightDirection.xyz);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    vec3 radiance = vec3(1.0, 0.98, 0.9) * 2.0; // Hardcoded Sun Color (TODO: Uniform)

    float NDF = DistributionGGX(N, H, Roughness);
    float G   = GeometrySmith(N, V, L, Roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - Metallic;

    // TODO: PCSS integration
    float shadow = 0.0;
    Lo += (1.0 - shadow) * (kD * Albedo / PI + specular) * radiance * NdotL;

    // --- IBL (IMAGE-BASED LIGHTING / AMBIENT) ---
    vec3 F_IBL = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, Roughness);
    vec3 kS_IBL = F_IBL;
    vec3 kD_IBL = 1.0 - kS_IBL;
    kD_IBL *= 1.0 - Metallic;

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse    = irradiance * Albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, Roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf  = texture(brdfMap, vec2(max(dot(N, V), 0.0), Roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F_IBL * brdf.x + brdf.y);

    vec3 ambient = (kD_IBL * diffuse + specularIBL) * AO;
    
    // --- DDGI FALLBACK (Skipped for now) ---
    // (If DDGI is available in the shader, add its ambient contribution here)

    vec3 color = ambient + Lo + Emissive;

    // --- TONEMAPPING & GAMMA CORRECTION (Handled in Post-Processing usually, but applying simple Reinhard here) ---
    // color = color / (color + vec3(1.0));
    // color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, 1.0);

    // Bloom Extraction
    float brightness = dot(outColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > 1.0) outBrightColor = vec4(outColor.rgb, 1.0);
    else outBrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}
