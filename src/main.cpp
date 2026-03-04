#include "core/Application.h"
#include "project/Project.h"
#include "renderer/Renderer.h"
#include "scripts/ScriptRegistry.h"
#include <iostream>
#include <filesystem>

// ==============================================================================
// L'INSTANCE UNIQUE DU REGISTRE DE SCRIPTS
// C'est le moteur qui possède la mémoire, le .so du jeu viendra écrire dedans !
// ==============================================================================
std::unordered_map<std::string, std::function<void(NativeScriptComponent&)>> ScriptRegistry::Registry;

int main(int argc, char** argv) {
    // 1. Création de la fenêtre et du contexte (L'EditorLayer est créé automatiquement ici)
    Application app("Cool Engine", 1600, 900);
    app.SetWindowIcon("icon.png");

    // 2. Initialisation des sous-systèmes (Rendu, Physique, etc.)
    Renderer::Init();

    // 3. Chargement d'un projet via ligne de commande (ex: "Play" depuis CLion)
    if (argc > 1) {
        std::filesystem::path projectPath = argv[1];

        // On vérifie que le fichier existe et que c'est bien un projet
        if (std::filesystem::exists(projectPath) && projectPath.extension() == ".ceproj") {

            // --- LA MAGIE ASYNCHRONE ---
            // On met le projet en file d'attente. L'EditorLayer va intercepter ça
            // à la première frame, lancer CMake, et afficher le Splash Screen.
            Project::LoadAsync(projectPath);

            std::cout << "[Engine] Projet mis en attente de compilation : " << projectPath << std::endl;
        } else {
            std::cerr << "[Engine] Argument invalide ou projet introuvable : " << projectPath << std::endl;
        }
    }

    // 4. Lancement de la boucle principale
    app.Run();

    return 0;
}