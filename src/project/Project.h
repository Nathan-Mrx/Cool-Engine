#pragma once
#include <filesystem>
#include <string>
#include <memory>
#include <vector>

struct ProjectConfig {
    std::string Name = "Untitled";
    std::string Version = "0.0.1";
    std::filesystem::path ProjectDirectory; // Racine (où se trouve le .ceproj)

    // Dossiers standards alignés sur l'industrie
    std::filesystem::path ContentDirectory;  // Tout le contenu du jeu
    std::filesystem::path ConfigDirectory;   // Préférences utilisateur
    std::filesystem::path BinariesDirectory; // Binaires compilés

    std::string StartScene = "Scenes/Default.cescene";
};

class Project {
public:
    static void New(const std::string& name, const std::filesystem::path& path);
    static std::shared_ptr<Project> Load(const std::filesystem::path& path);
    static void SaveActive(const std::filesystem::path& path);

    // Accesseurs de dossiers
    static const std::filesystem::path& GetProjectDirectory() { return s_ActiveProject->m_Config.ProjectDirectory; }
    static const std::filesystem::path& GetContentDirectory() { return s_ActiveProject->m_Config.ContentDirectory; }
    static const std::filesystem::path& GetConfigDirectory()  { return s_ActiveProject->m_Config.ConfigDirectory; }
    static const std::filesystem::path& GetBinariesDirectory() { return s_ActiveProject->m_Config.BinariesDirectory; }
    static std::filesystem::path GetCacheDirectory() { return s_ActiveProject->m_Config.ProjectDirectory / ".ce_cache"; }

    static const std::string& GetProjectName() { return s_ActiveProject->m_Config.Name; }
    static const std::string& GetProjectVersion() { return s_ActiveProject->m_Config.Version; }
    static std::shared_ptr<Project> GetActive() { return s_ActiveProject; }

    ProjectConfig& GetConfig() { return m_Config; }
    static void Unload();

    // Projets récents
    static std::vector<std::filesystem::path> GetRecentProjects();
    static void AddToRecentProjects(const std::filesystem::path& path);
    static void ValidateRecentProjects();

    static void RemoveFromHistory(const std::filesystem::path& path);

    // --- NOUVEAU SYSTÈME ASYNCHRONE ---
    static void LoadAsync(const std::filesystem::path& path);
    static std::filesystem::path ConsumePendingProject();

private:
    ProjectConfig m_Config;
    inline static std::shared_ptr<Project> s_ActiveProject;

    static inline std::filesystem::path s_PendingProjectPath;
};