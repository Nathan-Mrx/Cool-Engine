#pragma once
#include <filesystem>
#include <string>
#include <memory>

struct ProjectConfig {
    std::string Name = "Untitled";
    std::filesystem::path ProjectDirectory; // Le dossier racine du projet (ex: /home/nathan/MyGame)
    std::filesystem::path AssetDirectory;   // Le dossier des assets (ex: /home/nathan/MyGame/Assets)
    std::string StartScene = "Scenes/Default.scene";
};

class Project {
public:
    static const std::filesystem::path& GetProjectDirectory() { return s_ActiveProject->m_Config.ProjectDirectory; }

    static const std::filesystem::path& GetAssetDirectory() {
        static std::filesystem::path empty;
        if (!s_ActiveProject) return empty; // Retourne un chemin vide au lieu de crash
        return s_ActiveProject->m_Config.AssetDirectory;
    }

    static const std::string& GetProjectName() { return s_ActiveProject->m_Config.Name; }
    
    // Crée un nouveau projet vierge
    static std::shared_ptr<Project> New();
    
    // Charge un .ceproj existant
    static std::shared_ptr<Project> Load(const std::filesystem::path& path);
    
    // Sauvegarde la configuration actuelle
    static void SaveActive(const std::filesystem::path& path);

    static std::shared_ptr<Project> GetActive() { return s_ActiveProject; }

    ProjectConfig& GetConfig() { return m_Config; }



private:
    ProjectConfig m_Config;
    inline static std::shared_ptr<Project> s_ActiveProject;
};