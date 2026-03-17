#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// Forward declaration pour éviter d'inclure tout VulkanRenderer ici
struct VulkanTexture;

class DDGIVolume {
public:
    DDGIVolume(glm::vec3 startPosition, glm::ivec3 probeCount, glm::vec3 probeSpacing);
    ~DDGIVolume();

    // On renvoie directement des pointeurs vers nos textures Vulkan
    VulkanTexture* GetIrradianceTexture() const { return m_IrradianceTexture; }
    VulkanTexture* GetDepthTexture() const { return m_DepthTexture; }

    glm::ivec3 GetProbeCount() const { return m_ProbeCount; }
    glm::vec3 GetProbeSpacing() const { return m_ProbeSpacing; }
    glm::vec3 GetStartPosition() const { return m_StartPosition; }

private:
    void InitTextures();

    glm::vec3 m_StartPosition;
    glm::ivec3 m_ProbeCount;
    glm::vec3 m_ProbeSpacing;

    VulkanTexture* m_IrradianceTexture = nullptr;
    VulkanTexture* m_DepthTexture = nullptr;
};