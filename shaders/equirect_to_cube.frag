#version 460 core
out vec4 FragColor;
in vec3 vLocalPos;

uniform sampler2D uEquirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    float safeX = (abs(v.x) < 0.0001 && abs(v.y) < 0.0001) ? 0.0001 : v.x;
    vec2 uv = vec2(atan(v.y, safeX), asin(clamp(v.z, -0.9999, 0.9999)));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec3 v = normalize(vLocalPos);
    vec2 uv = SampleSphericalMap(v);
    vec3 color = texture(uEquirectangularMap, uv).rgb;

    // --- LE FIX DES SOLEILS NOIRS ---
    // Si la valeur déborde, c'est le soleil ! On force 50 000 au lieu de NOIR.
    if (isnan(color.r) || isinf(color.r)) color = vec3(50000.0);
    color = clamp(color, 0.0, 50000.0);

    FragColor = vec4(color, 1.0);
}