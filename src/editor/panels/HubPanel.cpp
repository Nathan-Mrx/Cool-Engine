#include "HubPanel.h"
#include "../../project/Project.h"
#include <imgui.h>
#include <nfd.hpp>

void HubPanel::OnImGuiRender() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 450));

    ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Cool Engine - Hub", nullptr, hubFlags);

    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("COOL ENGINE");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    
    ImGui::Columns(2, "HubColumns", false);
    ImGui::SetColumnWidth(0, 450);

    ImGui::Text("Recent Projects");
    auto recents = Project::GetRecentProjects();
    for (const auto& path : recents) {
        if (ImGui::Selectable(path.stem().string().c_str())) {
            Project::Load(path);
        }
    }

    ImGui::NextColumn();
    if (ImGui::Button("Open Project...", ImVec2(-1, 50))) {
        nfdchar_t* outPath = nullptr;
        if (NFD::OpenDialog(outPath, nullptr, 0, nullptr) == NFD_OKAY) {
            Project::Load(outPath);
            NFD::FreePath(outPath);
        }
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