#pragma once
#include <filesystem>
#include <map>
#include <vector>

class HubPanel {
public:
    void OnImGuiRender();
    uint32_t GetThumbnailTexture(const std::filesystem::path& path);

private:
    // Cache pour stocker les IDs de textures déjà chargées
    std::map<std::filesystem::path, uint32_t> m_ThumbnailCache;
    uint32_t m_DefaultProjectIcon = 0; // À initialiser avec une texture vide
};