#include "Project.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <algorithm> // Pour std::remove


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

        AddToRecentProjects(path);

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

    AddToRecentProjects(path);
}


// Fichier de configuration global du moteur (créé à côté de l'exécutable)
static const std::string EDITOR_CONFIG_FILE = "editor_config.json";

std::vector<std::filesystem::path> Project::GetRecentProjects() {
    std::vector<std::filesystem::path> recents;
    if (std::filesystem::exists(EDITOR_CONFIG_FILE)) {
        std::ifstream stream(EDITOR_CONFIG_FILE);
        if (stream.is_open()) {
            try {
                json data = json::parse(stream);
                if (data.contains("RecentProjects")) {
                    for (auto& pathStr : data["RecentProjects"]) {
                        recents.push_back(pathStr.get<std::string>());
                    }
                }
            } catch (json::parse_error& e) {
                std::cerr << "Failed to parse editor config: " << e.what() << std::endl;
            }
        }
    }
    return recents;
}

void Project::AddToRecentProjects(const std::filesystem::path& path) {
    auto recents = GetRecentProjects();

    // 1. Si le projet est déjà dans la liste, on le retire pour le remettre tout en haut
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());

    // 2. On l'insère au début de la liste
    recents.insert(recents.begin(), path);

    // 3. On garde un maximum de 10 projets récents
    if (recents.size() > 10) recents.resize(10);

    // 4. On sauvegarde le tout dans le JSON global
    json data;
    for (const auto& p : recents) {
        data["RecentProjects"].push_back(p.string());
    }

    std::ofstream stream(EDITOR_CONFIG_FILE);
    if (stream.is_open()) {
        stream << data.dump(4);
    }
}