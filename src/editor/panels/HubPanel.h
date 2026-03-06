#pragma once
#include <filesystem>
#include <map>
#include <vector>

class HubPanel {
public:
    void OnImGuiRender();
    uint32_t GetThumbnailTexture(const std::filesystem::path& path);

private:
    // --- SOUS-FONCTIONS DE RENDU (Refactoring) ---
    void DrawHeader();
    void DrawLeftColumn(bool& triggerNewProject);
    void DrawRightColumn();
    void DrawRecentProjectItem(const std::filesystem::path& projectPath, float thumbnailSize);
    void DrawNewProjectModal(bool& triggerNewProject);

private:
    // Cache pour stocker les IDs de textures déjà chargées
    std::map<std::filesystem::path, uint32_t> m_ThumbnailCache;
    uint32_t m_DefaultProjectIcon = 0;
};