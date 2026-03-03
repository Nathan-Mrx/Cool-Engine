#include "HubPanel.h"
#include "../../project/Project.h"
#include <imgui.h>
#include <nfd.hpp>
#include "renderer/TextureLoader.h"

void HubPanel::OnImGuiRender() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(800, 500)); // Un peu plus large pour la grille

    ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Cool Engine - Hub", nullptr, hubFlags);

    // Header
    auto& fonts = ImGui::GetIO().Fonts->Fonts;
    bool pushed = false;

    if (fonts.Size > 1) { // On ne push que si une deuxième police est chargée
        ImGui::PushFont(fonts[1]);
        pushed = true;
    } else {
        // Si pas de deuxième police, on utilise l'échelle visuelle comme avant
        ImGui::SetWindowFontScale(1.5f);
    }

    ImGui::Text("COOL ENGINE");

    if (pushed) {
        ImGui::PopFont();
    } else {
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Layout principal : 2 Colonnes
    if (ImGui::BeginTable("MainLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Recents", ImGuiTableColumnFlags_WidthFixed, 550.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        // --- COLONNE GAUCHE : GRILLE DE PROJETS ---
        ImGui::TextDisabled("RECENT PROJECTS");
        ImGui::BeginChild("RecentProjectsArea", ImVec2(0, 0), true);

        float cardWidth = 160.0f;
        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        auto recents = Project::GetRecentProjects(); //

        if (ImGui::BeginTable("ProjectGrid", 3)) { // 3 colonnes de cartes
            for (const auto& path : recents) {
                ImGui::TableNextColumn();

                ImGui::PushID(path.string().c_str());

                // Début de la Card
                ImGui::BeginGroup();

                // 1. Thumbnail (Placeholder ou image chargée)
                // Tu devras utiliser une classe Texture2D pour charger .ce_cache/thumbnail.png
                ImTextureID texID = (ImTextureID)(uintptr_t)GetThumbnailTexture(path);
                if (ImGui::ImageButton("##thumb", texID, ImVec2(cardWidth, cardWidth * 0.56f), ImVec2(0, 1), ImVec2(1, 0))) {
                    Project::Load(path);
                }

                // 2. Titre et Infos
                ImGui::TextWrapped("%s", path.stem().string().c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextWrapped("%s", path.parent_path().string().c_str());
                ImGui::PopStyleColor();

                ImGui::EndGroup();

                // Tooltip au survol pour voir le chemin complet
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", path.string().c_str());

                ImGui::PopID();
                ImGui::Spacing();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        // --- COLONNE DROITE : BOUTONS D'ACTION ---
        ImGui::TableNextColumn();
        ImGui::Spacing();

        if (ImGui::Button("Open Project...", ImVec2(-1, 40))) {
            nfdchar_t* outPath = nullptr;
            if (NFD::OpenDialog(outPath, nullptr, 0, nullptr) == NFD_OKAY) {
                Project::Load(outPath);
                NFD::FreePath(outPath);
            }
        }

        if (ImGui::Button("New Project...", ImVec2(-1, 40))) {
            ImGui::OpenPopup("NewProjectPopup");
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("New Project...", ImVec2(-1, 50))) {
        ImGui::OpenPopup("NewProjectPopup");
    }

    // Fenêtre surgissante pour configurer le nouveau projet
    if (ImGui::BeginPopupModal("NewProjectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char projectName[256] = "MyNewProject";
        ImGui::Text("Project Name:");
        ImGui::InputText("##name", projectName, IM_ARRAYSIZE(projectName));

        ImGui::Separator();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            nfdchar_t* outPath = nullptr;
            if (NFD::PickFolder(outPath, nullptr) == NFD_OKAY) {
                // Cette ligne ne devrait plus générer d'erreur après la mise à jour du .h
                Project::New(projectName, outPath);
                NFD::FreePath(outPath);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}

uint32_t HubPanel::GetThumbnailTexture(const std::filesystem::path& path) {
    if (m_ThumbnailCache.find(path) != m_ThumbnailCache.end()) {
        return m_ThumbnailCache[path];
    }

    std::filesystem::path thumbPath = path.parent_path() / ".ce_cache/thumbnail.png";

    if (std::filesystem::exists(thumbPath)) {
        // On utilise notre nouvel utilitaire
        uint32_t textureID = TextureLoader::LoadTexture(thumbPath.string());
        if (textureID != 0) {
            m_ThumbnailCache[path] = textureID;
            return textureID;
        }
    }

    return m_DefaultProjectIcon;
}