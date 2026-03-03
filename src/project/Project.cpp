#include "Project.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <algorithm>

using json = nlohmann::json;

std::shared_ptr<Project> Project::New(const std::string& name, const std::filesystem::path& path) {
    std::filesystem::path projectFolder = path / name;

    try {
        std::filesystem::create_directories(projectFolder / "Content");
        std::filesystem::create_directories(projectFolder / "Config");

        // Création du fichier JSON
        nlohmann::json projectJson;
        // On s'assure que la structure correspond à ce que Load() attend
        projectJson["Name"] = name;
        projectJson["StartScene"] = "Scenes/Default.cescene";

        // CORRECTION : On repasse sur l'extension .ceproj
        std::filesystem::path projectFilePath = projectFolder / (name + ".ceproj");

        std::ofstream projectFile(projectFilePath);
        if (projectFile.is_open()) {
            projectFile << projectJson.dump(4);
            projectFile.close();
        }

        return Load(projectFilePath);
    } catch (const std::exception& e) {
        return nullptr;
    }
}

std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
    std::filesystem::path absolutePath = std::filesystem::absolute(path);

    if (!std::filesystem::exists(absolutePath)) return nullptr;

    std::ifstream stream(path);
    if (!stream.is_open()) return nullptr;

    try {
        json data = json::parse(stream);
        std::shared_ptr<Project> project = std::make_shared<Project>();

        project->m_Config.ProjectDirectory = absolutePath.parent_path();
        project->m_Config.ContentDirectory = project->m_Config.ProjectDirectory / "Content";
        project->m_Config.ConfigDirectory = project->m_Config.ProjectDirectory / "Config";
        project->m_Config.BinariesDirectory = project->m_Config.ProjectDirectory / "Binaries";

        // --- LE FIX : GESTION DES ANCIENS ET NOUVEAUX PROJETS ---
        if (data.contains("Project")) {
            auto& projData = data["Project"];
            project->m_Config.Name = projData.value("Name", "Untitled");
            project->m_Config.StartScene = projData.value("StartScene", "Scenes/Default.cescene");
        } else {
            // Rétrocompatibilité pour tes anciens projets !
            project->m_Config.Name = data.value("Name", "Untitled");
            project->m_Config.StartScene = data.value("StartScene", "Scenes/Default.cescene");
        }

        s_ActiveProject = project;
        AddToRecentProjects(path);

        return s_ActiveProject;
    }
    catch (json::parse_error& e) {
        std::cerr << "Parse error: " << e.what() << std::endl;
        return nullptr;
    }
}

void Project::SaveActive(const std::filesystem::path& path) {
    if (!s_ActiveProject) return;

    nlohmann::json data;
    data["Project"]["Name"] = s_ActiveProject->m_Config.Name;
    data["Project"]["StartScene"] = s_ActiveProject->m_Config.StartScene;

    std::ofstream stream(path);
    if (stream.is_open()) {
        stream << data.dump(4);
    }

    AddToRecentProjects(path);
}

// --- LOGIQUE DES PROJETS RÉCENTS ---

static const std::string EDITOR_CONFIG_FILE = "editor_config.json";

void Project::Unload()
{
    s_ActiveProject = nullptr;
    std::cout << "[Project] Project unloaded." << std::endl;
}

std::vector<std::filesystem::path> Project::GetRecentProjects() {
    std::vector<std::filesystem::path> recents;
    if (!std::filesystem::exists(EDITOR_CONFIG_FILE)) return recents;

    std::ifstream stream(EDITOR_CONFIG_FILE);
    if (stream.is_open()) {
        try {
            json data = json::parse(stream);
            if (data.contains("RecentProjects")) {
                for (auto& pathStr : data["RecentProjects"])
                    recents.push_back(pathStr.get<std::string>());
            }
        } catch (...) {}
    }
    return recents;
}

void Project::AddToRecentProjects(const std::filesystem::path& path) {
    auto recents = GetRecentProjects();
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
    recents.insert(recents.begin(), path);
    if (recents.size() > 10) recents.resize(10);

    json data;
    for (const auto& p : recents) data["RecentProjects"].push_back(p.string());

    std::ofstream stream(EDITOR_CONFIG_FILE);
    if (stream.is_open()) stream << data.dump(4);
}

void Project::ValidateRecentProjects() {
    auto recents = GetRecentProjects(); // On récupère la copie
    size_t initialSize = recents.size();

    // Filtrage des chemins inexistants
    auto it = std::remove_if(recents.begin(), recents.end(), [](const std::filesystem::path& path) {
        return !std::filesystem::exists(path);
    });

    // On ne sauvegarde QUE si la taille a changé (optimisation I/O)
    if (it != recents.end()) {
        recents.erase(it, recents.end());

        // --- LE FIX : SAUVEGARDE SUR DISQUE ---
        nlohmann::json data;
        for (const auto& p : recents)
            data["RecentProjects"].push_back(p.string());

        std::ofstream stream(EDITOR_CONFIG_FILE);
        if (stream.is_open()) {
            stream << data.dump(4);
            std::cout << "[Project] History cleaned and saved." << std::endl;
        }
    }
}

void Project::RemoveFromHistory(const std::filesystem::path& path) {
    auto recents = GetRecentProjects();

    // On retire le chemin spécifique de la liste
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());

    // On réécrit le fichier de configuration
    nlohmann::json data;
    for (const auto& p : recents)
        data["RecentProjects"].push_back(p.string());

    std::ofstream stream(EDITOR_CONFIG_FILE);
    if (stream.is_open()) {
        stream << data.dump(4);
        std::cout << "[Project] Project removed from history: " << path << std::endl;
    }
}