#include "../panels/ContentBrowserPanel.h"

#include <fstream>

#include "../../project/Project.h"
#include <imgui.h>

#include "scene/Entity.h"
#include "scene/SceneSerializer.h"

ContentBrowserPanel::ContentBrowserPanel() {}

void ContentBrowserPanel::OnImGuiRender() {
    static bool s_OpenCreateMaterialPopup = false;
    static char s_NewMaterialName[128] = "NewMaterial";

    ImGui::Begin("Content Browser");

    if (!Project::GetActive()) {
        ImGui::Text("No Project Active");
        ImGui::End();
        return;
    }

    if (m_CurrentDirectory.empty()) {
        m_CurrentDirectory = Project::GetContentDirectory();
    }

    // --- 1. TOP BAR ---
    std::filesystem::path contentDir = Project::GetContentDirectory();

    // Bouton Retour
    if (m_CurrentDirectory != contentDir) {
        if (ImGui::Button("<-")) {
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
        }
        ImGui::SameLine();
    }

    // Bouton Nouveau Dossier
    if (ImGui::Button("New Folder")) {
        ImGui::OpenPopup("NewFolderPopup");
    }

    if (ImGui::BeginPopup("NewFolderPopup")) {
        static char folderName[128] = "NewFolder";
        ImGui::InputText("##Name", folderName, sizeof(folderName));
        if (ImGui::Button("Create")) {
            std::filesystem::create_directory(m_CurrentDirectory / folderName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Chemin relatif
    std::filesystem::path relativePath = std::filesystem::relative(m_CurrentDirectory, Project::GetProjectDirectory());
    ImGui::Text("%s", relativePath.string().c_str());
    ImGui::Separator();

    // --- 2. GRID LAYOUT (Style Unreal) ---
    float padding = 16.0f;
    float thumbnailSize = 90.0f;
    float cellSize = thumbnailSize + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    if (std::filesystem::exists(m_CurrentDirectory)) {
        for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
            const auto& path = directoryEntry.path();
            std::string filename = path.filename().string();

            ImGui::PushID(filename.c_str());

            if (directoryEntry.is_directory()) {
                // Rendu Dossier
                ImGui::Button(filename.c_str(), { thumbnailSize, thumbnailSize });

                // Double clic pour ouvrir
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_CurrentDirectory /= path.filename();
                }
            } else {
                // Rendu Fichier
                bool isScene = path.extension() == ".cescene";
                bool isPrefab = path.extension() == ".ceprefab"; // <-- NOUVEAU

                // --- VISIBILITÉ : Couleurs différentes ---
                if (isScene) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
                else if (isPrefab) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.2f, 1.0f)); // Orange pour les prefabs !

                ImGui::Button(filename.c_str(), { thumbnailSize, thumbnailSize });

                if (isScene || isPrefab) ImGui::PopStyleColor();

                // --- LOGIQUE D'OUVERTURE CORRIGÉE ---
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (isScene && OnSceneOpenCallback) {
                        OnSceneOpenCallback(path);
                    } else if (isPrefab && OnPrefabOpenCallback) {
                        OnPrefabOpenCallback(path);
                    } else if (path.extension() == ".cemat" && OnMaterialOpenCallback) {
                        OnMaterialOpenCallback(path);
                    }
                }

                // DÉBUT DU DRAG AND DROP (Source)
                if (ImGui::BeginDragDropSource()) {
                    std::string itemPath = path.string();
                    // On définit un identifiant "CONTENT_BROWSER_ITEM" et on envoie le chemin absolu en mémoire
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
                    ImGui::Text("Drop %s", filename.c_str()); // Tooltip qui suit la souris
                    ImGui::EndDragDropSource();
                }
            }

            ImGui::TextWrapped("%s", filename.c_str());
            ImGui::NextColumn();
            ImGui::PopID();
        }
    }

    // --- MENU CONTEXTUEL (Clic Droit dans le vide) ---
    if (ImGui::BeginPopupContextWindow("ContentBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Prefab")) {
            std::filesystem::path newPrefabPath = m_CurrentDirectory / "NewPrefab.ceprefab";

            // --- LE FIX : On crée une vraie scène avec une racine par défaut ---
            auto tempScene = std::make_shared<Scene>();
            tempScene->CreateEntity("NewPrefab Root"); // La racine garantie !

            SceneSerializer serializer(tempScene);
            serializer.Serialize(newPrefabPath.string());
        }
        if (ImGui::MenuItem("Material")) {
            s_OpenCreateMaterialPopup = true;
            strcpy(s_NewMaterialName, "NewMaterial"); // Valeur par défaut
        }
        ImGui::EndMenu();
    }

    // 1. On ordonne à ImGui d'ouvrir le Popup (au centre de l'écran)
    if (s_OpenCreateMaterialPopup) {
        ImGui::OpenPopup("Name New Material");
        s_OpenCreateMaterialPopup = false; // On reset le déclencheur
    }

    // 2. Le vrai contenu du Popup
    if (ImGui::BeginPopupModal("Name New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter material name:");
        ImGui::InputText("##MatName", s_NewMaterialName, sizeof(s_NewMaterialName));
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            std::string nameStr = s_NewMaterialName;
            if (!nameStr.empty()) {
                // On fabrique le chemin complet (Dossier Actuel / NomDuMateriau.cemat)
                std::filesystem::path newMatPath = m_CurrentDirectory / (nameStr + ".cemat");

                // IMPORTANT : On injecte un JSON vide valide, sinon l'éditeur plantera en essayant de le lire !
                std::ofstream file(newMatPath);
                file << "{\n    \"Type\": \"MaterialGraph\",\n    \"NextID\": 1,\n    \"Nodes\": [],\n    \"Links\": []\n}";
                file.close();

                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SetItemDefaultFocus(); // Permet d'appuyer sur "Entrée"
        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::Columns(1);
    ImGui::End();
}