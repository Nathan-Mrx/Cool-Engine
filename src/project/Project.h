#pragma once
#include <filesystem>
#include <string>
#include <memory>
#include <vector>

struct ProjectConfig {
    std::string Name = "Untitled";
    std::filesystem::path ProjectDirectory; // Racine (où se trouve le .ceproj)

    // Dossiers standards alignés sur l'industrie
    std::filesystem::path ContentDirectory;  // Tout le contenu du jeu
    std::filesystem::path ConfigDirectory;   // Préférences utilisateur
    std::filesystem::path BinariesDirectory; // Binaires compilés

    std::string StartScene = "Scenes/Default.cescene";
};

class Project {
public:
    static std::shared_ptr<Project> New();
    static std::shared_ptr<Project> Load(const std::filesystem::path& path);
    static void SaveActive(const std::filesystem::path& path);

    // Accesseurs de dossiers
    static const std::filesystem::path& GetProjectDirectory() { return s_ActiveProject->m_Config.ProjectDirectory; }
    static const std::filesystem::path& GetContentDirectory() { return s_ActiveProject->m_Config.ContentDirectory; }
    static const std::filesystem::path& GetConfigDirectory()  { return s_ActiveProject->m_Config.ConfigDirectory; }
    static const std::filesystem::path& GetBinariesDirectory() { return s_ActiveProject->m_Config.BinariesDirectory; }

    static const std::string& GetProjectName() { return s_ActiveProject->m_Config.Name; }
    static std::shared_ptr<Project> GetActive() { return s_ActiveProject; }

    ProjectConfig& GetConfig() { return m_Config; }

    // Projets récents
    static std::vector<std::filesystem::path> GetRecentProjects();
    static void AddToRecentProjects(const std::filesystem::path& path);

private:
    ProjectConfig m_Config;
    inline static std::shared_ptr<Project> s_ActiveProject;
};