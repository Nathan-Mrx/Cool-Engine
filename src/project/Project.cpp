#include "Project.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <dlfcn.h> // Indispensable pour dlopen/dlclose !
#include <imgui.h>

using json = nlohmann::json;

// --- VARIABLE GLOBALE POUR LE HOT-RELOAD ---
static void* s_GameModuleHandle = nullptr;

void Project::New(const std::string& name, const std::filesystem::path& path) {
    std::filesystem::path projectFolder = path / name;

    try {
        // 1. Création de l'architecture complète du projet
        std::filesystem::create_directories(projectFolder / "Content");
        std::filesystem::create_directories(projectFolder / "Config");
        std::filesystem::create_directories(projectFolder / "Source");
        std::filesystem::create_directories(projectFolder / "Binaries");
        std::filesystem::create_directories(projectFolder / ".ce-cache");

        // On force la résolution du chemin absolu basé sur le répertoire d'exécution (cmake-build-debug)
        std::filesystem::path engineIniPath = std::filesystem::current_path() / "imgui.ini";
        std::filesystem::path projectIniPath = projectFolder / "imgui.ini";

        if (std::filesystem::exists(engineIniPath)) {
            std::filesystem::copy_file(engineIniPath, projectIniPath, std::filesystem::copy_options::overwrite_existing);
            std::cout << "[Project] Layout ImGui copie avec succes depuis : " << engineIniPath << std::endl;
        } else {
            std::cout << "[Project] Info : Aucun imgui.ini trouve a copier depuis : " << engineIniPath << std::endl;
        }

        // 2. Génération automatique du CMakeLists.txt du Jeu !
        std::ofstream cmakeFile(projectFolder / "CMakeLists.txt");
        if (cmakeFile.is_open()) {
            cmakeFile << "cmake_minimum_required(VERSION 3.20)\n";
            cmakeFile << "project(" << name << ")\n\n";
            cmakeFile << "set(CMAKE_CXX_STANDARD 20)\n";
            cmakeFile << "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n\n";
            cmakeFile << "# --- CHEMINS DU MOTEUR ---\n";
            cmakeFile << "set(ENGINE_DIR \"/home/nathan/CLionProjects/GameEngine\")\n";
            cmakeFile << "set(GAME_SOURCE_DIR \"${CMAKE_SOURCE_DIR}/Source\")\n";
            cmakeFile << "set(GENERATED_CPP \"${CMAKE_BINARY_DIR}/GeneratedScripts.cpp\")\n\n";
            cmakeFile << "# --- COOL HEADER TOOL ---\n";
            cmakeFile << "add_custom_command(\n";
            cmakeFile << "    OUTPUT ${GENERATED_CPP}\n";
            cmakeFile << "    COMMAND python3 ${ENGINE_DIR}/tools/CoolHeaderTool.py ${GAME_SOURCE_DIR} ${GENERATED_CPP}\n";
            cmakeFile << "    COMMENT \"Generation du registre des scripts via CHT...\"\n";
            cmakeFile << ")\n\n";
            cmakeFile << "# --- COMPILATION DU MODULE (.so) ---\n";
            cmakeFile << "add_library(GameModule SHARED ${GENERATED_CPP})\n";
            cmakeFile << "target_include_directories(GameModule PRIVATE \n";
            cmakeFile << "    ${ENGINE_DIR}/src\n";
            cmakeFile << "    ${ENGINE_DIR}/cmake-build-debug/vcpkg_installed/x64-linux/include\n";
            cmakeFile << ")\n";
            cmakeFile << "target_include_directories(GameModule PRIVATE ${ENGINE_DIR}/src)\n";
            cmakeFile << "set_target_properties(GameModule PROPERTIES \n";
            cmakeFile << "    PREFIX \"\" # Pour générer GameModule.so au lieu de libGameModule.so\n";
            cmakeFile << "    LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/Binaries\"\n";
            cmakeFile << ")\n";
            cmakeFile.close();
        }

        // 3. Création du fichier JSON .ceproj
        nlohmann::json projectJson;
        projectJson["Name"] = name;
        projectJson["Version"] = "0.0.1";
        projectJson["StartScene"] = "Scenes/Default.cescene";

        std::filesystem::path projectFilePath = projectFolder / (name + ".ceproj");

        std::ofstream projectFile(projectFilePath);
        if (projectFile.is_open()) {
            projectFile << projectJson.dump(4);
            projectFile.close();
        }

        LoadAsync(projectFilePath);
    } catch (const std::exception& e) {
        std::cerr << "Erreur de création" << std::endl;
    }
}

std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
    std::filesystem::path absolutePath = std::filesystem::absolute(path);

    if (!std::filesystem::exists(absolutePath)) return nullptr;

    std::ifstream file(absolutePath);
    if (!file.is_open()) return nullptr;

    json data;
    file >> data;

    std::shared_ptr<Project> project = std::make_shared<Project>();

    // --- LE FIX : Support des deux formats (Ancien buggué et Nouveau propre) ---
    if (data.contains("Project")) {
        project->m_Config.Name = data["Project"].value("Name", "Untitled");
        project->m_Config.Version = data["Project"].value("Version", "0.0.1");
        project->m_Config.StartScene = data["Project"].value("StartScene", "Scenes/Default.cescene");
    } else {
        project->m_Config.Name = data.value("Name", "Untitled");
        project->m_Config.Version = data.value("Version", "0.0.1");
        project->m_Config.StartScene = data.value("StartScene", "Scenes/Default.cescene");
    }

    std::filesystem::path projectDir = absolutePath.parent_path();
    project->m_Config.ProjectDirectory = projectDir;
    project->m_Config.ContentDirectory = projectDir / "Content";
    project->m_Config.ConfigDirectory = projectDir / "Config";
    project->m_Config.BinariesDirectory = projectDir / "Binaries";

    s_ActiveProject = project;

    // ===============================================================
    // CHARGEMENT À CHAUD DU MODULE DU JEU (.so)
    // ===============================================================
    std::string modulePath = (project->m_Config.BinariesDirectory / "GameModule.so").string();

    if (std::filesystem::exists(modulePath)) {
        if (s_GameModuleHandle) {
            dlclose(s_GameModuleHandle);
            s_GameModuleHandle = nullptr;
        }

        s_GameModuleHandle = dlopen(modulePath.c_str(), RTLD_NOW | RTLD_GLOBAL);

        if (s_GameModuleHandle) {
            std::cout << "[Project] Module charge avec succes : " << modulePath << std::endl;
        } else {
            std::cerr << "[Project] ERREUR lors du chargement du module : " << dlerror() << std::endl;
        }
    } else {
        std::cout << "[Project] Aucun GameModule.so trouve. (C'est normal si le jeu n'a pas encore ete compile !)" << std::endl;
    }

    static std::string s_ProjectIniPath;
    s_ProjectIniPath = (project->m_Config.ProjectDirectory / "imgui.ini").string();
    ImGui::GetIO().IniFilename = s_ProjectIniPath.c_str();
    ImGui::LoadIniSettingsFromDisk(s_ProjectIniPath.c_str());

    AddToRecentProjects(absolutePath);
    return project;
}

void Project::SaveActive(const std::filesystem::path& path) {
    if (!s_ActiveProject) return;

    nlohmann::json data;
    // --- LE FIX EST ICI : On sauvegarde à la racine (sans le bloc "Project") ---
    // Cela correspond parfaitement à la logique de New() et Load() !
    data["Name"] = s_ActiveProject->m_Config.Name;
    data["Version"] = s_ActiveProject->m_Config.Version;
    data["StartScene"] = s_ActiveProject->m_Config.StartScene;

    std::ofstream stream(path);
    if (stream.is_open()) {
        stream << data.dump(4);
    }

    AddToRecentProjects(path);
}

// --- LOGIQUE DES PROJETS RÉCENTS ---

static const std::string EDITOR_CONFIG_FILE = "editor_config.json";

void Project::Unload() {
    s_ActiveProject = nullptr;

    // Retour au layout global du moteur
    static std::string s_EngineIniPath = (std::filesystem::current_path() / "imgui.ini").string();
    ImGui::GetIO().IniFilename = s_EngineIniPath.c_str();
    ImGui::LoadIniSettingsFromDisk(s_EngineIniPath.c_str());
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

void Project::LoadAsync(const std::filesystem::path& path) {
    s_PendingProjectPath = path;
}

std::filesystem::path Project::ConsumePendingProject() {
    std::filesystem::path temp = s_PendingProjectPath;
    s_PendingProjectPath = "";
    return temp;
}