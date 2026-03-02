#include "Project.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::shared_ptr<Project> Project::New() {
    s_ActiveProject = std::make_shared<Project>();
    return s_ActiveProject;
}

std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        std::cerr << "Project file not found: " << path << std::endl;
        return nullptr;
    }

    std::ifstream stream(path);
    if (!stream.is_open()) {
        std::cerr << "Failed to open project file: " << path << std::endl;
        return nullptr;
    }

    try {
        json data = json::parse(stream);
        
        std::shared_ptr<Project> project = std::make_shared<Project>();
        
        // On récupère le dossier où se trouve le fichier .ceproj
        project->m_Config.ProjectDirectory = path.parent_path();
        
        project->m_Config.Name = data.value("Name", "Untitled");
        // Le chemin de l'AssetDirectory est relatif au ProjectDirectory
        std::string assetDirStr = data.value("AssetDirectory", "Assets");
        project->m_Config.AssetDirectory = project->m_Config.ProjectDirectory / assetDirStr;
        project->m_Config.StartScene = data.value("StartScene", "Scenes/Default.scene");

        s_ActiveProject = project;
        return s_ActiveProject;
    }
    catch (json::parse_error& e) {
        std::cerr << "Parse error in project file: " << e.what() << std::endl;
        return nullptr;
    }
}

void Project::SaveActive(const std::filesystem::path& path) {
    if (!s_ActiveProject) return;

    json data;
    data["Name"] = s_ActiveProject->m_Config.Name;
    
    // On sauvegarde un chemin relatif (ex: "Assets") et non le chemin absolu CachyOS
    auto relativeAssetDir = std::filesystem::relative(s_ActiveProject->m_Config.AssetDirectory, s_ActiveProject->m_Config.ProjectDirectory);
    data["AssetDirectory"] = relativeAssetDir.string();
    
    data["StartScene"] = s_ActiveProject->m_Config.StartScene;

    std::ofstream stream(path);
    if (stream.is_open()) {
        stream << data.dump(4); // Indentation de 4 espaces pour un JSON lisible
    }
}