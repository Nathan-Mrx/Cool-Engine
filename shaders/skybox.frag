#version 460 core
out vec4 FragColor;

in vec3 vWorldPos;
uniform sampler2D uEquirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = SampleSphericalMap(normalize(vWorldPos));
    vec3 color = texture(uEquirectangularMap, uv).rgb;

    // --- PROTECTION SOLAIRE ---
    // Si un pixel est infini, on le transforme en un soleil très très blanc au lieu de noir !
    if (isnan(color.r) || isinf(color.r)) color = vec3(50000.0);

    // On limite la luminosité maximale juste avant l'affichage
    color = clamp(color, 0.0, 50000.0);

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}