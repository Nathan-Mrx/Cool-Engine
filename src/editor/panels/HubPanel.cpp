#include "HubPanel.h"
#include "../../project/Project.h"
#include <imgui.h>
#include <nfd.hpp>

#include "renderer/RendererAPI.h"
#include "renderer/TextureLoader.h"

// =========================================================================================
// ENTRY POINT DU RENDU UI
// =========================================================================================
void HubPanel::OnImGuiRender() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(800, 500));

    ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Cool Engine - Hub", nullptr, hubFlags);

    bool triggerNewProject = false;

    DrawHeader();

    ImGui::Spacing();

    if (ImGui::BeginTable("MainLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        // --- COLONNE GAUCHE (Actions) ---
        ImGui::TableNextColumn();
        DrawLeftColumn(triggerNewProject);

        // --- COLONNE DROITE (Projets Récents) ---
        ImGui::TableNextColumn();
        DrawRightColumn();

        ImGui::EndTable();
    }

    DrawNewProjectModal(triggerNewProject);

    ImGui::End();
}

// =========================================================================================
// 1. EN-TÊTE
// =========================================================================================
void HubPanel::DrawHeader() {
    auto& fonts = ImGui::GetIO().Fonts->Fonts;
    bool pushed = false;
    if (fonts.Size > 1) {
        ImGui::PushFont(fonts[1]);
        pushed = true;
    } else {
        ImGui::SetWindowFontScale(1.5f);
    }

    ImGui::Text("COOL ENGINE");

    if (pushed) {
        ImGui::PopFont();
    } else {
        ImGui::SetWindowFontScale(1.0f);
    }

    ImGui::Separator();
}

// =========================================================================================
// 2. COLONNE GAUCHE (Boutons d'Action)
// =========================================================================================
void HubPanel::DrawLeftColumn(bool& triggerNewProject) {
    ImGui::TextDisabled("Actions");
    ImGui::Spacing();

    if (ImGui::Button("New Project", ImVec2(-1, 40))) {
        triggerNewProject = true;
    }

    ImGui::Spacing();

    if (ImGui::Button("Open Project", ImVec2(-1, 40))) {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filterItem[1] = { { "Cool Engine Project", "ceproj" } };

        if (NFD::OpenDialog(outPath, filterItem, 1, nullptr) == NFD_OKAY) {
            Project::Load(outPath);
            NFD::FreePath(outPath);
        }
    }
}

// =========================================================================================
// 3. COLONNE DROITE (Liste des Projets Récents)
// =========================================================================================
void HubPanel::DrawRightColumn() {
    ImGui::TextDisabled("Recent Projects");
    ImGui::Spacing();

    if (ImGui::BeginChild("RecentProjectsList", ImVec2(0, 0), true)) {
        std::vector<std::filesystem::path> recents = Project::GetRecentProjects();
        float thumbnailSize = 64.0f;

        if (recents.empty()) {
            ImGui::TextDisabled("No recent projects found.");
        } else {
            for (const auto& path : recents) {
                DrawRecentProjectItem(path, thumbnailSize);
            }
        }
    }
    ImGui::EndChild();
}

void HubPanel::DrawRecentProjectItem(const std::filesystem::path& projectPath, float thumbnailSize) {
    ImGui::PushID(projectPath.string().c_str());

    ImVec2 rowSize = ImVec2(ImGui::GetContentRegionAvail().x, thumbnailSize + 10);
    ImGui::InvisibleButton("##RowBtn", rowSize);

    bool isHovered = ImGui::IsItemHovered();
    bool isClicked = ImGui::IsItemClicked();

    if (isHovered) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(50, 50, 50, 255));
    }

    ImGui::SetCursorPos(ImVec2(ImGui::GetItemRectMin().x - ImGui::GetWindowPos().x + ImGui::GetScrollX(), ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y + ImGui::GetScrollY() + 5));

    // Thumbnail
    void* texID = GetThumbnailTexture(projectPath);
    if (texID != nullptr && RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        ImGui::Image((ImTextureID)texID, ImVec2(thumbnailSize, thumbnailSize), ImVec2(0, 1), ImVec2(1, 0));
    } else {
        ImGui::Button("NO\nIMG", ImVec2(thumbnailSize, thumbnailSize));
    }

    // Textes
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::Text("%s", projectPath.stem().string().c_str());
    ImGui::TextDisabled("%s", projectPath.string().c_str());
    ImGui::EndGroup();

    if (isClicked) {
        Project::Load(projectPath);
    }

    ImGui::PopID();
}

// =========================================================================================
// 4. MODAL NOUVEAU PROJET
// =========================================================================================
void HubPanel::DrawNewProjectModal(bool& triggerNewProject) {
    if (triggerNewProject) {
        ImGui::OpenPopup("NewProjectPopup");
    }

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
}

void* HubPanel::GetThumbnailTexture(const std::filesystem::path& path) {
    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) return nullptr;

    if (m_ThumbnailCache.find(path) != m_ThumbnailCache.end()) {
        return m_ThumbnailCache[path];
    }

    std::filesystem::path thumbPath = path.parent_path() / ".ce_cache/thumbnail.png";

    if (std::filesystem::exists(thumbPath)) {
        void* textureID = TextureLoader::LoadTexture(thumbPath.string().c_str());
        if (textureID != nullptr) {
            m_ThumbnailCache[path] = textureID;
            return textureID;
        }
    }

    return m_DefaultProjectIcon;
}