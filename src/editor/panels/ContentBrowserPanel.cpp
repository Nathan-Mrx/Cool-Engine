#include "../panels/ContentBrowserPanel.h"

#include <fstream>

#include "../../project/Project.h"
#include <imgui.h>

#include "editor/AssetRegistry.h"
#include "renderer/TextureLoader.h"
#include "scene/Entity.h"
#include "scene/SceneSerializer.h"
#include <vector>
#include <algorithm>

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

    // Chargement paresseux de l'icône de dossier
    if (m_DirectoryIcon == 0 && std::filesystem::exists("icons/folder.png")) {
        m_DirectoryIcon = TextureLoader::LoadTexture("icons/folder.png");
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

    // --- 2. GRID LAYOUT & SORTING ---
    float padding = 16.0f;
    float thumbnailSize = 90.0f;
    float cellSize = thumbnailSize + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    if (std::filesystem::exists(m_CurrentDirectory)) {

        // ========================================================
        // ÉTAPE A : RÉCUPÉRATION ET TRI DES FICHIERS
        // ========================================================
        std::vector<std::filesystem::directory_entry> entries;
        for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
            entries.push_back(directoryEntry);
        }

        std::sort(entries.begin(), entries.end(), [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b) {
            bool aIsDir = a.is_directory();
            bool bIsDir = b.is_directory();

            // 1. Les dossiers en premier
            if (aIsDir && !bIsDir) return true;
            if (!aIsDir && bIsDir) return false;

            // 2. Trier par type (extension)
            if (!aIsDir && !bIsDir) {
                std::string extA = a.path().extension().string();
                std::string extB = b.path().extension().string();
                if (extA != extB) return extA < extB;
            }

            // 3. Trier par nom alphabétique
            return a.path().filename().string() < b.path().filename().string();
        });

        // ========================================================
        // ÉTAPE B : RENDU DES CARTES
        // ========================================================
        for (auto& directoryEntry : entries) {
            const auto& path = directoryEntry.path();
            std::string filename = path.filename().string();

            ImGui::PushID(filename.c_str());

            if (directoryEntry.is_directory()) {
                // --- RENDU DOSSIER ---
                if (m_DirectoryIcon != 0) {
                    // On utilise "##Dir" pour que ImGui ne dessine pas de texte dans le bouton
                    ImGui::ImageButton("##Dir", (ImTextureID)(uintptr_t)m_DirectoryIcon, { thumbnailSize, thumbnailSize }, ImVec2(0, 1), ImVec2(1, 0), ImVec4(0,0,0,0));
                } else {
                    // Fallback si pas d'icône : un dossier vide avec écrit "DIR" au milieu
                    ImGui::Button("DIR", { thumbnailSize, thumbnailSize });
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_CurrentDirectory /= path.filename();
                }
            } else {
                // --- RENDU ASSET ---
                AssetTypeInfo info;
                bool isKnownAsset = AssetRegistry::GetInfo(path.extension().string(), info);

                // Couleur de fond de la carte
                ImVec4 bgCol = isKnownAsset ? ImVec4(info.Color.x * 0.4f, info.Color.y * 0.4f, info.Color.z * 0.4f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, bgCol);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bgCol.x * 1.5f, bgCol.y * 1.5f, bgCol.z * 1.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(bgCol.x * 0.8f, bgCol.y * 0.8f, bgCol.z * 0.8f, 1.0f));

                if (isKnownAsset && info.IconID != 0) {
                    // Bouton avec l'icône bien centrée et fond coloré
                    ImGui::ImageButton("##Asset", (ImTextureID)(uintptr_t)info.IconID, { thumbnailSize, thumbnailSize }, ImVec2(0, 1), ImVec2(1, 0), bgCol);
                } else {
                    // Fallback si pas d'icône : on écrit l'extension (ex: ".cewav") au milieu du bouton !
                    std::string ext = path.extension().string();
                    if (ext.empty()) ext = "FILE";
                    ImGui::Button(ext.c_str(), { thumbnailSize, thumbnailSize });
                }

                ImGui::PopStyleColor(3);

                // Logique d'ouverture
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (path.extension() == ".cescene" && OnSceneOpenCallback) OnSceneOpenCallback(path);
                    else if (path.extension() == ".ceprefab" && OnPrefabOpenCallback) OnPrefabOpenCallback(path);
                    else if (path.extension() == ".cemat" && OnMaterialOpenCallback) OnMaterialOpenCallback(path);
                }

                // Drag and Drop
                if (ImGui::BeginDragDropSource()) {
                    std::string itemPath = path.string();
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
                    ImGui::Text("Drop %s", filename.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            // LE TEXTE EN DESSOUS DE L'ICÔNE
            ImGui::TextWrapped("%s", filename.c_str());
            ImGui::NextColumn();
            ImGui::PopID();
        }
    }

    // --- MENU CONTEXTUEL (Clic Droit dans le vide) ---
    // --- MENU CONTEXTUEL GÉNÉRIQUE ---
    if (ImGui::BeginPopupContextWindow("ContentBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("Create...")) {
            // MAGIE : On lit tous les types d'assets du registre !
            for (const auto& [ext, info] : AssetRegistry::GetRegistry()) {
                if (ImGui::MenuItem(info.Name.c_str())) {
                    m_OpenCreateAssetPopup = true;
                    m_CreateAssetType = info.Name;
                    m_CreateAssetExtension = info.Extension;
                    snprintf(m_NewAssetName, sizeof(m_NewAssetName), "New%s", info.Name.c_str());
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // --- ON APPELLE NOTRE NOUVELLE MACHINE À LA TOUTE FIN ! ---
    DrawCreateAssetPopup();

    ImGui::Columns(1);
    ImGui::End();
}

// --- LE MOTEUR GÉNÉRIQUE DE POPUP ---
void ContentBrowserPanel::DrawCreateAssetPopup() {
    if (m_OpenCreateAssetPopup) {
        ImGui::OpenPopup("Name New Asset");
        m_OpenCreateAssetPopup = false; // On reset le déclencheur pour ne pas ouvrir en boucle
    }

    if (ImGui::BeginPopupModal("Name New Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter %s name:", m_CreateAssetType.c_str());
        ImGui::InputText("##AssetName", m_NewAssetName, sizeof(m_NewAssetName));
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            std::string nameStr = m_NewAssetName;
            if (!nameStr.empty()) {
                std::filesystem::path newAssetPath = m_CurrentDirectory / (nameStr + m_CreateAssetExtension);

                // === L'ASSET SE CRÉE LUI-MÊME ! ===
                AssetTypeInfo info;
                if (AssetRegistry::GetInfo(m_CreateAssetExtension, info) && info.Instance) {
                    info.Instance->CreateDefaultAsset(newAssetPath);
                } else {
                    // Fallback de sécurité : fichier vide
                    std::ofstream file(newAssetPath);
                    file.close();
                }

                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}