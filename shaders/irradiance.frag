#version 460 core
out vec4 FragColor;
in vec3 vLocalPos;

uniform samplerCube uEnvironmentMap;
const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(vLocalPos);
    vec3 irradiance = vec3(0.0);

    vec3 up = vec3(0.0, 1.0, 0.0);
    // Sécurité pour ne pas faire un "cross" entre deux vecteurs identiques
    if (abs(N.y) > 0.999) {
        up = vec3(0.0, 0.0, 1.0);
    }

    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    // Un pas un peu plus grand (0.05) = environ 3800 échantillons au lieu de 15000.
    // C'est largement suffisant pour une irradiance parfaite et très rapide à générer !
    float sampleDelta = 0.05;
    float nrSamples = 0.0;

    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            // --- LE VACCIN ANTI-CARRÉS ET ANTI-FLICKERING ---
            vec3 envColor = texture(uEnvironmentMap, sampleVec).rgb;
            envColor = min(envColor, vec3(10.0)); // On écrase l'énergie nucléaire du soleil !

            irradiance += envColor * cos(theta) * sin(theta);
        }
    }

    if (nrSamples > 0.0) {
        irradiance = PI * irradiance * (1.0 / nrSamples);
    }

    FragColor = vec4(irradiance, 1.0);
}