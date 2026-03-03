#include "Project.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <algorithm>

using json = nlohmann::json;

std::shared_ptr<Project> Project::New() {
    s_ActiveProject = std::make_shared<Project>();
    return s_ActiveProject;
}

std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return nullptr;

    std::ifstream stream(path);
    if (!stream.is_open()) return nullptr;

    try {
        json data = json::parse(stream);
        std::shared_ptr<Project> project = std::make_shared<Project>();

        // 1. Définition des chemins de base
        project->m_Config.ProjectDirectory = path.parent_path();
        project->m_Config.ContentDirectory = project->m_Config.ProjectDirectory / "Content";
        project->m_Config.ConfigDirectory  = project->m_Config.ProjectDirectory / "Config";
        project->m_Config.BinariesDirectory = project->m_Config.ProjectDirectory / "Binaries";

        // 2. Chargement des métadonnées
        project->m_Config.Name = data.value("Name", "Untitled");
        project->m_Config.StartScene = data.value("StartScene", "Scenes/Default.cescene");

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

    json data;
    data["Name"] = s_ActiveProject->m_Config.Name;
    data["StartScene"] = s_ActiveProject->m_Config.StartScene;

    std::ofstream stream(path);
    if (stream.is_open()) {
        stream << data.dump(4);
    }

    AddToRecentProjects(path);
}

// --- LOGIQUE DES PROJETS RÉCENTS ---

static const std::string EDITOR_CONFIG_FILE = "editor_config.json";

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