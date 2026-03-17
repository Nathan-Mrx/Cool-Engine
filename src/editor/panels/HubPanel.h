#pragma once
#include <filesystem>
#include <map>
#include <vector>

class HubPanel {
public:
    void OnImGuiRender();
    void* GetThumbnailTexture(const std::filesystem::path& path);

private:
    // --- SOUS-FONCTIONS DE RENDU (Refactoring) ---
    void DrawHeader();
    void DrawLeftColumn(bool& triggerNewProject);
    void DrawRightColumn();
    void DrawRecentProjectItem(const std::filesystem::path& projectPath, float thumbnailSize);
    void DrawNewProjectModal(bool& triggerNewProject);

private:
    // Cache pour stocker les IDs de textures déjà chargées (Maintenant en void*)
    std::map<std::filesystem::path, void*> m_ThumbnailCache;
    void* m_DefaultProjectIcon = nullptr;
};