#version 450
layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform samplerCube environmentMap;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(fragWorldPos);
    vec3 irradiance = vec3(0.0);

    // --- LE FIX EST ICI ---
    // Si N pointe presque parfaitement vers le haut (Z), on change l'axe de référence vers X !
    vec3 up    = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    // On réduit légèrement la précision (0.025 prenait 15 000 échantillons, 0.05 est parfait et plus rapide)
    float sampleDelta = 0.05;
    float nrSamples = 0.0;

    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += texture(environmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));

    outColor = vec4(irradiance, 1.0);
}