#include "DDGIVolume.h"
#include "VulkanRenderer.h" // Obligatoire pour créer les textures
#include <iostream>

DDGIVolume::DDGIVolume(glm::vec3 startPosition, glm::ivec3 probeCount, glm::vec3 probeSpacing)
    : m_StartPosition(startPosition), m_ProbeCount(probeCount), m_ProbeSpacing(probeSpacing)
{
    InitTextures();
    std::cout << "[DDGI] Volume cree : "
              << (probeCount.x * probeCount.y * probeCount.z) << " sondes actives." << std::endl;
}

DDGIVolume::~DDGIVolume() {
    // On nettoie proprement la VRAM via notre Renderer
    if (m_IrradianceTexture) VulkanRenderer::Get()->DestroyTexture(m_IrradianceTexture);
    if (m_DepthTexture) VulkanRenderer::Get()->DestroyTexture(m_DepthTexture);
}

void DDGIVolume::InitTextures() {
    int probesPerRow = m_ProbeCount.x * m_ProbeCount.y;
    int probesPerCol = m_ProbeCount.z;

    // --- 1. TEXTURE D'IRRADIANCE (Couleur diffuse) ---
    // 8x8 pixels par sonde (6x6 données + 1 px de bordure)
    int irradWidth = probesPerRow * 8;
    int irradHeight = probesPerCol * 8;

    // VK_FORMAT_R16G16B16A16_SFLOAT = GL_RGBA16F
    m_IrradianceTexture = VulkanRenderer::Get()->CreateStorageTexture(irradWidth, irradHeight, VK_FORMAT_R16G16B16A16_SFLOAT);

    // --- 2. TEXTURE DE PROFONDEUR (Visibilité / Ombres GI) ---
    // 16x16 pixels par sonde
    int depthWidth = probesPerRow * 16;
    int depthHeight = probesPerCol * 16;

    // VK_FORMAT_R16G16_SFLOAT = GL_RG16F
    m_DepthTexture = VulkanRenderer::Get()->CreateStorageTexture(depthWidth, depthHeight, VK_FORMAT_R16G16_SFLOAT);
}