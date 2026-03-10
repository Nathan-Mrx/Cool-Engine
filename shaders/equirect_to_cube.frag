#version 450
layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D equirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

// La même fonction Z-Up qu'on a utilisée pour les matériaux !
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = SampleSphericalMap(normalize(fragWorldPos));
    vec3 color = texture(equirectangularMap, uv).rgb;
    outColor = vec4(color, 1.0);
}