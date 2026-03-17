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

const float PI = 3.14159265359;

// ==========================================================
// --- MATHÉMATIQUES DDGI ---
// ==========================================================
vec2 OctWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * mix(vec2(-1.0), vec2(1.0), step(vec2(0.0), v));
}
vec2 OctEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    return n.xy;
}

// ==========================================================
// --- PBR MATHS ---
// ==========================================================
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
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ==========================================================
// --- OMBRES PCSS (AVEC UN VRAI FLOU VISIBLE !) ---
// ==========================================================
vec2 VogelDisk(int sampleIndex, int samplesCount, float phi) {
    float r = sqrt(float(sampleIndex) + 0.5) / sqrt(float(samplesCount));
    float theta = float(sampleIndex) * 2.39996323 + phi;
    return vec2(r * cos(theta), r * sin(theta));
}

float InterleavedGradientNoise(vec2 positionScreen) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(positionScreen, magic.xy)));
}

float ShadowCalculation(vec3 fragPosWorld, vec3 N, vec3 L) {
    float depthValue = length(ubo.cameraPos.xyz - fragPosWorld);

    int layer = 3;
    for (int i = 0; i < 4; ++i) {
        if (depthValue < ubo.cascadeSplits[i]) {
            layer = i;
            break;
        }
    }

    vec4 fragPosLightSpace = ubo.lightSpaceMatrices[layer] * vec4(fragPosWorld, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
    if (layer > 0) bias *= 1.5;

    int NUM_SAMPLES = 16;
    float shadow = 0.0;
    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    // CORRECTION : On monte le rayon de flou à 15 pixels pour que ce soit bien visible en 4K !
    float filterRadius = (layer == 0) ? 15.0 : 8.0;

    for(int i = 0; i < NUM_SAMPLES; i++) {
        vec2 offset = VogelDisk(i, NUM_SAMPLES, noise * 2.0 * PI) * texelSize * filterRadius;
        float pcfDepth = texture(shadowMap, vec3(projCoords.xy + offset, layer)).r;
        shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    }
    return shadow / float(NUM_SAMPLES);
}

void main() {
    // 1. TEXTURES & COULEURS
    vec4 albedoTex = texture(albedoMap, fragTexCoord);
    vec3 albedo = pow(albedoTex.rgb, vec3(2.2)) * ubo.baseColor.rgb;
    float metallic = texture(metallicMap, fragTexCoord).r * ubo.metallic;
    float roughness = max(texture(roughnessMap, fragTexCoord).r * ubo.roughness, 0.04);
    float aoTex = texture(aoMap, fragTexCoord).r;
    float finalAO = aoTex * ubo.ao;

    // 2. NORMAL MAPPING (RESTAURÉ !)
    vec3 N = normalize(fragNormal);
    if (length(fragTangent) > 0.1) {
        vec3 T = normalize(fragTangent);
        T = normalize(T - dot(T, N) * N);
        vec3 B = cross(N, T);
        mat3 TBN = mat3(T, B, N);
        vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // 3. SOLEIL & OMBRES DIRECTES (Direction et couleur drivees par la Directional Light)
    vec3 L = normalize(ubo.lightDirection.xyz);
    vec3 H = normalize(V + L);

    float shadow = ShadowCalculation(fragWorldPos, N, L);
    vec3 radiance = ubo.lightColor.xyz * (1.0 - shadow);
    float NdotL = max(dot(N, L), 0.0);

    vec3 Lo = vec3(0.0);
    if (NdotL > 0.0 && shadow < 1.0) {
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

    // 4. ILLUMINATION GLOBALE (AMBIANCE)
    vec3 F_ibl = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = vec3(1.0) - kS_ibl;
    kD_ibl *= 1.0 - metallic;

    vec3 ddgiIrradiance = vec3(0.0);

    // LECTURE DDGI (Ray Tracing)
    if (ubo.ddgiProbeCount.x > 0 && ubo.ddgiStartPosition.w > 0.0) {
        vec3 gridCoord = (fragWorldPos - ubo.ddgiStartPosition.xyz) / ubo.ddgiStartPosition.w;
        ivec3 baseProbeCoords = ivec3(floor(gridCoord));
        vec3 alpha = fract(gridCoord);

        float weightSum = 0.0;
        int probesPerRow = ubo.ddgiProbeCount.x * ubo.ddgiProbeCount.y;

        for (int i = 0; i < 8; ++i) {
            ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
            ivec3 probeCoord = clamp(baseProbeCoords + offset, ivec3(0), ubo.ddgiProbeCount.xyz - 1);
            int probeIndex = probeCoord.x + probeCoord.y * ubo.ddgiProbeCount.x + probeCoord.z * probesPerRow;
            int gridX = probeIndex % probesPerRow;
            int gridY = probeIndex / probesPerRow;

            vec2 octUV = OctEncode(N) * 0.5 + 0.5;
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

    // --- LE SAUVEUR : LE CIEL PROCÉDURAL ---
    // Si l'Irradiance C++ est noire, on génère un ciel bleu/gris artificiel
    vec3 staticIrradiance = texture(irradianceMap, N).rgb;
    if (any(isnan(staticIrradiance)) || length(staticIrradiance) < 0.001) {
        staticIrradiance = mix(vec3(0.05, 0.05, 0.05), vec3(0.2, 0.5, 0.8), N.y * 0.5 + 0.5);
    }

    if (length(ddgiIrradiance) < 0.01) {
        ddgiIrradiance = staticIrradiance;
    }

    vec3 diffuseIBL = ddgiIrradiance * albedo;

    // REFLETS (Prefilter Map)
    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 envColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;

    // Si la texture de reflet C++ est noire ou NaN, on génère un beau ciel brillant
    if (any(isnan(envColor)) || length(envColor) < 0.001) {
        envColor = mix(vec3(0.05, 0.05, 0.05), vec3(0.5, 0.7, 1.0), R.y * 0.5 + 0.5);
    }

    vec2 brdf = texture(brdfMap, vec2(max(dot(N, V), 0.0), roughness)).rg;

    vec3 specularIBL = envColor * (F_ibl * brdf.x + brdf.y);

    // Eclairage ambiant final !
    vec3 ambient = (kD_ibl * diffuseIBL + specularIBL) * finalAO;

    // COULEUR FINALE (Directe + Indirecte)
    vec3 color = Lo + ambient;

    float exposure = 0.5; // Change cette valeur pour équilibrer ta scène (0.1 à 1.0)
    color = color * exposure;

    // TONEMAPPING ACES
    float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    color = clamp((color*(a*color+b))/(color*(c*color+d)+e), 0.0, 1.0);
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, ubo.baseColor.a);


}