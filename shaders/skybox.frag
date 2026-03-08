#version 460 core
out vec4 FragColor;

in vec3 vWorldPos;
uniform sampler2D uEquirectangularMap;

uniform float uIntensity;
uniform float uRotation;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

// --- LE MÊME TONEMAPPER QUE TES MATÉRIAUX ---
vec3 ACESFilm(vec3 x) {
    float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec3 pos = normalize(vWorldPos);

    // --- ROTATION SUR L'AXE Z (Z-Up) ---
    float c = cos(uRotation);
    float s = sin(uRotation);
    pos.xy = vec2(pos.x * c - pos.y * s, pos.x * s + pos.y * c);

    vec2 uv = SampleSphericalMap(pos);
    vec3 color = texture(uEquirectangularMap, uv).rgb;

    if (isnan(color.r) || isinf(color.r)) color = vec3(0.0);

    // --- INTENSITÉ ---
    color *= uIntensity;

    // --- TONEMAPPING AAA ---
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}