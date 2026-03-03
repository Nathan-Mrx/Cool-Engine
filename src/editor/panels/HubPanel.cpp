#include "HubPanel.h"
#include "../../project/Project.h"
#include <imgui.h>
#include <nfd.hpp>
#include "renderer/TextureLoader.h"

void HubPanel::OnImGuiRender() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(800, 500));

    ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Cool Engine - Hub", nullptr, hubFlags);

    // --- LOGIQUE DE POPUP ---
    bool triggerNewProject = false; // Flag pour déclencher l'ouverture

    // Header (Logique de police conservée)
    auto& fonts = ImGui::GetIO().Fonts->Fonts;
    bool pushed = false;
    if (fonts.Size > 1) { ImGui::PushFont(fonts[1]); pushed = true; }
    else { ImGui::SetWindowFontScale(1.5f); }

    ImGui::Text("COOL ENGINE");

    if (pushed) { ImGui::PopFont(); }
    else { ImGui::SetWindowFontScale(1.0f); }

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("MainLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Recents", ImGuiTableColumnFlags_WidthFixed, 550.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        // --- COLONNE GAUCHE : GRILLE DE PROJETS ---
        ImGui::TextDisabled("RECENT PROJECTS");
        ImGui::BeginChild("RecentProjectsArea", ImVec2(0, 0), true);

        // Validation unique au démarrage sur CachyOS
        static bool firstFrame = true;
        if (firstFrame) { Project::ValidateRecentProjects(); firstFrame = false; }

        auto recents = Project::GetRecentProjects();
        float cardWidth = 160.0f;

        if (ImGui::BeginTable("ProjectGrid", 3)) {
            for (const auto& path : recents) {
                ImGui::TableNextColumn();
                ImGui::PushID(path.string().c_str());

                ImGui::BeginGroup(); // On groupe l'image et le texte pour le clic droit

                ImTextureID texID = (ImTextureID)(uintptr_t)GetThumbnailTexture(path);
                if (ImGui::ImageButton("##thumb", texID, ImVec2(cardWidth, cardWidth * 0.56f), ImVec2(0, 1), ImVec2(1, 0))) {
                    Project::Load(path);
                }

                ImGui::TextWrapped("%s", path.stem().string().c_str());

                ImGui::EndGroup(); // Fin du groupe "Card"

                // --- LE FIX : MENU CONTEXTUEL CLIC DROIT ---
                // S'applique au groupe (Image + Texte)
                if (ImGui::BeginPopupContextItem("ProjectCardMenu")) {
                    if (ImGui::MenuItem("Forget Project")) {
                        Project::RemoveFromHistory(path);

                        // OPTIONNEL : Nettoyer aussi la texture du cache pour libérer de la VRAM
                        if (m_ThumbnailCache.count(path)) {
                            // glDeleteTextures(1, &m_ThumbnailCache[path]); // Si tu veux être très propre
                            m_ThumbnailCache.erase(path);
                        }
                    }
                    ImGui::EndPopup();
                }

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

        // Le bouton "joli" utilise maintenant le flag
        if (ImGui::Button("New Project...", ImVec2(-1, 40))) {
            triggerNewProject = true;
        }

        ImGui::EndTable();
    }

    // --- LE DEUXIÈME BOUTON A ÉTÉ SUPPRIMÉ ---

    // --- DÉCLENCHEMENT DU POPUP À LA RACINE ---
    if (triggerNewProject) {
        ImGui::OpenPopup("NewProjectPopup");
    }

    // Définition du Modal
    if (ImGui::BeginPopupModal("NewProjectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char projectName[256] = "MyNewProject";
        ImGui::Text("Project Name:");
        ImGui::InputText("##name", projectName, IM_ARRAYSIZE(projectName));

        ImGui::Separator();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            nfdchar_t* outPath = nullptr;
            if (NFD::PickFolder(outPath, nullptr) == NFD_OKAY) {
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