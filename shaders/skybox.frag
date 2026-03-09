#version 450

layout(location = 0) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// Notre image HDR envoyée par Vulkan
layout(binding = 0) uniform sampler2D hdrMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

// Le même tonemapper que tes matériaux
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec3 pos = normalize(fragWorldPos);

    // Lecture de l'image HDR (on triche pour l'intensité et la rotation pour l'instant)
    vec2 uv = SampleSphericalMap(pos);
    vec3 color = texture(hdrMap, uv).rgb;

    // Intensité basique
    color *= 1.0;

    // Tonemapping AAA
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, 1.0);
}