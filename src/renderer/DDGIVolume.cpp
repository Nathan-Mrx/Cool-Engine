#include "DDGIVolume.h"
#include <glad/glad.h>
#include <iostream>

DDGIVolume::DDGIVolume(glm::vec3 startPosition, glm::ivec3 probeCount, glm::vec3 probeSpacing)
    : m_StartPosition(startPosition), m_ProbeCount(probeCount), m_ProbeSpacing(probeSpacing) 
{
    InitTextures();
    std::cout << "[DDGI] Volume cree : " 
              << (probeCount.x * probeCount.y * probeCount.z) << " sondes actives." << std::endl;
}

DDGIVolume::~DDGIVolume() {
    if (m_IrradianceTexture) glDeleteTextures(1, &m_IrradianceTexture);
    if (m_DepthTexture) glDeleteTextures(1, &m_DepthTexture);
}

void DDGIVolume::InitTextures() {
    // On aligne toutes les sondes sur une gigantesque texture 2D
    // Pour ça, on dit que la largeur de l'image = sondesX * sondesY, et la hauteur = sondesZ
    int probesPerRow = m_ProbeCount.x * m_ProbeCount.y;
    int probesPerCol = m_ProbeCount.z;

    // --- 1. TEXTURE D'IRRADIANCE (Couleur diffuse) ---
    // Chaque sonde prend 8x8 pixels (6x6 de données + 1 pixel de bordure de chaque côté pour éviter le filtrage baveux)
    int irradWidth = probesPerRow * 8;
    int irradHeight = probesPerCol * 8;

    glGenTextures(1, &m_IrradianceTexture);
    glBindTexture(GL_TEXTURE_2D, m_IrradianceTexture);
    // GL_RGBA16F pour stocker de la lumière HDR (très intense)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, irradWidth, irradHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // --- 2. TEXTURE DE PROFONDEUR (Visibilité / Ombres GI) ---
    // Chaque sonde prend 16x16 pixels (14x14 de données + bordures)
    int depthWidth = probesPerRow * 16;
    int depthHeight = probesPerCol * 16;

    glGenTextures(1, &m_DepthTexture);
    glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
    // GL_RG16F car on a juste besoin de stocker "Distance" et "Distance au carré" (Algorithme de Chebyshev)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, depthWidth, depthHeight, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}