#version 460 core
out vec4 FragColor;

in vec3 vWorldPos;
uniform sampler2D uEquirectangularMap;

// --- NOUVELLES VARIABLES ---
uniform float uIntensity;
uniform float uRotation;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec3 pos = normalize(vWorldPos);

    // --- ROTATION SUR L'AXE Z (Z-Up) ---
    float c = cos(uRotation);
    float s = sin(uRotation);
    pos.xy = vec2(pos.x * c - pos.y * s, pos.x * s + pos.y * c);

    vec2 uv = SampleSphericalMap(pos);
    vec3 color = texture(uEquirectangularMap, uv).rgb;

    if (isnan(color.r) || isinf(color.r)) color = vec3(50000.0);
    color = clamp(color, 0.0, 50000.0);

    // --- INTENSITÉ ---
    color *= uIntensity;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}