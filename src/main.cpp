#include "core/Application.h"
#include "project/Project.h"
#include "renderer/Renderer.h"
#include "scripts/ScriptRegistry.h"
#include <iostream>
#include <filesystem>

// L'INSTANCE UNIQUE DU REGISTRE DE SCRIPTS
std::unordered_map<std::string, std::function<void(NativeScriptComponent&)>> ScriptRegistry::Registry;

int main(int argc, char** argv) {
    // 1. Sauvegarder le chemin du projet (ABSOLU) AVANT de changer de dossier de travail
    std::filesystem::path projectPathToLoad;
    if (argc > 1) {
        projectPathToLoad = std::filesystem::absolute(argv[1]);
    }

    // 2. Fixer le Working Directory sur le dossier du Moteur
    // Cela répare instantanément le bug du Viewport blanc car "shaders/" sera toujours trouvé !
    std::filesystem::path exePath = argv[0];
    if (exePath.is_relative()) exePath = std::filesystem::absolute(exePath);
    std::filesystem::current_path(exePath.parent_path());

    // 3. Lancement normal du moteur
    Application app("Cool Engine", 1600, 900);
    app.SetWindowIcon("icon.png");

    Renderer::Init();

    if (!projectPathToLoad.empty()) {
        if (std::filesystem::exists(projectPathToLoad) && projectPathToLoad.extension() == ".ceproj") {
            Project::LoadAsync(projectPathToLoad);
            std::cout << "[Engine] Projet mis en attente : " << projectPathToLoad << std::endl;
        } else {
            std::cerr << "[Engine] Projet introuvable : " << projectPathToLoad << std::endl;
        }
    }

    app.Run();

    return 0;
}