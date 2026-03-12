#version 450
// VULKAN

// =========================================================================================
// PIPELINE INPUTS (Vertex -> Fragment)
// =========================================================================================
layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inViewPos;
layout(location = 3) in vec3 inFragPos;
layout(location = 4) in vec3 inColor;
layout(location = 5) in vec4 inFragPosLightSpace[4];
layout(location = 9) in float inClipSpaceZ;

// =========================================================================================
// PIPELINE OUTPUTS
// =========================================================================================
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBrightColor;

// =========================================================================================
// STANDARD UNIFORMS (UBO)
// =========================================================================================
layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    vec4 cameraPos;
    vec4 cascadingSplits;
    
    mat4 lightSpaceMatrices[4];

    float metallic;
    float roughness;
    float ao;
    float _pad;

    vec4 ddgiStartPosition;
    ivec4 ddgiProbeCount;
} ubo;

// =========================================================================================
// PBR & ENVIRONMENT SAMPLERS (Set 1)
// =========================================================================================
layout(set = 1, binding = 0) uniform samplerCube uPrefilterMap;   // HDR Reflections
layout(set = 1, binding = 1) uniform sampler2D uBRDFLUT;          // BRDF LookUp
layout(set = 1, binding = 2) uniform samplerCube uIrradianceMap;  // Ambient Lighting
layout(set = 1, binding = 3) uniform sampler2DArray uShadowMap;   // Cascaded Shadows

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

// =========================================================================================
// SHADOW MAPPING
// =========================================================================================
float ShadowCalculation(vec3 fragPosWorldSpace, vec3 normal, vec3 lightDir) {
    int cascadeIndex = 0;
    for(int i = 0; i < 4 - 1; ++i) {
        if(inClipSpaceZ <= ubo.cascadingSplits[i]) cascadeIndex = i + 1;
    }

    vec4 fragPosLightSpace = inFragPosLightSpace[cascadeIndex];
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if(projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    if (cascadeIndex == 1) bias *= 1.5;
    if (cascadeIndex == 2) bias *= 2.0;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    const int PCF_RANGE = 2;
    float samples = 0.0;
    for(int x = -PCF_RANGE; x <= PCF_RANGE; ++x) {
        for(int y = -PCF_RANGE; y <= PCF_RANGE; ++y) {
            float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, cascadeIndex)).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            samples += 1.0;
        }
    }
    shadow /= samples;
    return shadow;
}

// =========================================================================================
// MAIN ENTRY POINT
// =========================================================================================
void main() {
    // --------------------------------------------------------------------------
    // DEFAULT MATERIAL PROPERTIES
    // --------------------------------------------------------------------------
    vec3  Albedo    = ubo.baseColor.rgb * inColor;
    vec3  Normal    = inNormal;
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
    vec3 V = normalize(ubo.cameraPos.xyz - inFragPos);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, Albedo, Metallic);

    vec3 Lo = vec3(0.0);

    // --- DIRECTIONAL SUN LIGHT ---
    vec3 L = normalize(vec3(-0.5, -1.0, -0.5)); // Hardcoded Sun Direction (TODO: Uniform)
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

    float shadow = ShadowCalculation(inFragPos, N, L);
    Lo += (1.0 - shadow) * (kD * Albedo / PI + specular) * radiance * NdotL;

    // --- IBL (IMAGE-BASED LIGHTING / AMBIENT) ---
    vec3 F_IBL = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, Roughness);
    vec3 kS_IBL = F_IBL;
    vec3 kD_IBL = 1.0 - kS_IBL;
    kD_IBL *= 1.0 - Metallic;

    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuse    = irradiance * Albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(uPrefilterMap, R, Roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf  = texture(uBRDFLUT, vec2(max(dot(N, V), 0.0), Roughness)).rg;
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
