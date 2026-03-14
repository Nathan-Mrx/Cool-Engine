#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

// Optionnel : L'ancienne HDR Map pour la nuit, ou un fond
layout(set = 0, binding = 0) uniform sampler2D hdrMap;

// Notre nouvelle UBO !
layout(set = 0, binding = 1) uniform SkyboxUBO {
    mat4 view;
    mat4 proj;
    vec4 sunDirection; // xyz = direction, w = intensity

    float planetRadius;
    float atmosphereRadius;
    float rayleighScaleHeight;
    float mieScaleHeight;

    vec4 rayleighScattering; // xyz = color, w = g (Mie preferred direction)
    float mieScattering;

    float useHDR; // 1.0 = sample HDR texture, 0.0 = procedural sky
    float hdrIntensity; // Exposure multiplier
    float hdrRotation;  // Rotation in radians
} ubo;

#define PI 3.14159265358979323846

// Conversion direction 3D -> UV equirectangulaire
vec2 SampleSphericalMap(vec3 v) {
    // Z-UP: on utilise v.xy pour le plan horizontal, v.z pour la hauteur
    vec2 uv = vec2(atan(v.y, v.x), asin(clamp(v.z, -1.0, 1.0)));
    uv *= vec2(0.1591, 0.3183); // 1/(2*PI), 1/PI
    uv += 0.5;
    return uv;
}

// Rotation Z-UP (autour de l'axe Z)
vec3 rotateAroundZ(vec3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

// Intersection sphère
vec2 raySphereIntersect(vec3 r0, vec3 rd, vec3 s0, float sr) {
    vec3 a = r0 - s0;
    float b = 2.0 * dot(rd, a);
    float c = dot(a, a) - sr * sr;
    float d = b * b - 4.0 * c;
    if (d < 0.0) return vec2(-1.0);
    d = sqrt(d);
    return vec2(-b - d, -b + d) / 2.0;
}

// Phase Rayleigh
float phaseRayleigh(float costh) {
    return 3.0 / (16.0 * PI) * (1.0 + costh * costh);
}

// Phase Mie (Henyey-Greenstein)
float phaseMie(float costh, float g) {
    float g2 = g * g;
    float a = 3.0 * (1.0 - g2) * (1.0 + costh * costh);
    float b = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * costh, 1.5);
    return a / b;
}

vec3 calculateScattering(vec3 rayStart, vec3 rayDir, vec3 sunDir) {
    // Centre de la planète (Z-UP : la planète est sous nos pieds sur l'axe Z)
    vec3 planetCenter = vec3(0.0, 0.0, -ubo.planetRadius);
    
    // Distances d'intersection avec l'atmosphère
    vec2 tAtmo = raySphereIntersect(rayStart, rayDir, planetCenter, ubo.atmosphereRadius);
    if (tAtmo.y < 0.0) return vec3(0.0); // Le rayon pointe dans l'espace vide
    
    float start = max(tAtmo.x, 0.0);
    float end = tAtmo.y;
    
    // Si la caméra frappe la planète
    vec2 tPlanet = raySphereIntersect(rayStart, rayDir, planetCenter, ubo.planetRadius);
    if (tPlanet.x > 0.0) end = min(end, tPlanet.x);

    // Initialisation
    int numSamples = 16;
    int numSamplesLight = 8;
    float stepSize = (end - start) / float(numSamples);
    
    float time = start + stepSize * 0.5;
    
    vec3 totalRayleigh = vec3(0.0);
    vec3 totalMie = vec3(0.0);
    
    float opticalDepthRayleigh = 0.0;
    float opticalDepthMie = 0.0;
    
    float costh = dot(rayDir, sunDir);
    float phaseR = phaseRayleigh(costh);
    float phaseM = phaseMie(costh, ubo.rayleighScattering.w); // g est stocké dans w
    
    vec3 B_r = ubo.rayleighScattering.xyz;
    vec3 B_m = vec3(ubo.mieScattering);
    
    for (int i = 0; i < numSamples; i++) {
        vec3 samplePos = rayStart + rayDir * time;
        float height = length(samplePos - planetCenter) - ubo.planetRadius;
        
        // Densité locale
        float hr = exp(-height / ubo.rayleighScaleHeight) * stepSize;
        float hm = exp(-height / ubo.mieScaleHeight) * stepSize;
        opticalDepthRayleigh += hr;
        opticalDepthMie += hm;
        
        // Rayonnement secondaire (vers le soleil)
        float stepSizeLight = raySphereIntersect(samplePos, sunDir, planetCenter, ubo.atmosphereRadius).y / float(numSamplesLight);
        float timeLight = stepSizeLight * 0.5;
        
        float opticalDepthLightRayleigh = 0.0;
        float opticalDepthLightMie = 0.0;
        
        bool inEarth = false;
        
        for (int j = 0; j < numSamplesLight; j++) {
            vec3 samplePosLight = samplePos + sunDir * timeLight;
            float heightLight = length(samplePosLight - planetCenter) - ubo.planetRadius;
            
            if (heightLight < 0.0) {
                inEarth = true;
                break;
            }
            
            opticalDepthLightRayleigh += exp(-heightLight / ubo.rayleighScaleHeight) * stepSizeLight;
            opticalDepthLightMie += exp(-heightLight / ubo.mieScaleHeight) * stepSizeLight;
            
            timeLight += stepSizeLight;
        }
        
        if (!inEarth) {
            vec3 tau = B_r * (opticalDepthRayleigh + opticalDepthLightRayleigh) + B_m * 1.1 * (opticalDepthMie + opticalDepthLightMie);
            vec3 attenuation = exp(-tau);
            
            totalRayleigh += hr * attenuation;
            totalMie += hm * attenuation;
        }
        
        time += stepSize;
    }
    
    return ubo.sunDirection.w * (totalRayleigh * B_r * phaseR + totalMie * B_m * phaseM);
}

vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec3 rayDir = normalize(fragWorldPos);
    
    if (ubo.useHDR > 0.5) {
        // Mode HDR : Equirectangular mapping avec rotation et intensite
        vec3 rotatedDir = rotateAroundZ(rayDir, ubo.hdrRotation);
        vec2 uv = SampleSphericalMap(rotatedDir);
        vec3 color = texture(hdrMap, uv).rgb * ubo.hdrIntensity;
        
        // Tonemapping AAA (ACES)
        color = ACESFilm(color);
        
        // Correction gamma
        color = pow(color, vec3(1.0/2.2));
        
        outColor = vec4(color, 1.0);
    } else {
        // Mode procedurale : Sky Atmosphere
        vec3 sunDir = normalize(ubo.sunDirection.xyz);
        vec3 color = calculateScattering(vec3(0.0), rayDir, sunDir);
        
        // Tonemapping AAA (ACES)
        color = ACESFilm(color);
        
        // Correction gamma
        color = pow(color, vec3(1.0/2.2));
        
        outColor = vec4(color, 1.0);
    }
}