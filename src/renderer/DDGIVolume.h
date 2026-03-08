#pragma once
#include <glm/glm.hpp>
#include <cstdint>

class DDGIVolume {
public:
    DDGIVolume(glm::vec3 startPosition, glm::ivec3 probeCount, glm::vec3 probeSpacing);
    ~DDGIVolume();

    // Getters pour envoyer aux shaders plus tard
    uint32_t GetIrradianceTexture() const { return m_IrradianceTexture; }
    uint32_t GetDepthTexture() const { return m_DepthTexture; }
    
    glm::ivec3 GetProbeCount() const { return m_ProbeCount; }
    glm::vec3 GetProbeSpacing() const { return m_ProbeSpacing; }
    glm::vec3 GetStartPosition() const { return m_StartPosition; }

private:
    void InitTextures();

    glm::vec3 m_StartPosition; // La position en bas à gauche de la grille
    glm::ivec3 m_ProbeCount;   // Combien de sondes en X, Y et Z
    glm::vec3 m_ProbeSpacing;  // Distance entre chaque sonde (en centimètres)

    uint32_t m_IrradianceTexture = 0;
    uint32_t m_DepthTexture = 0;
};