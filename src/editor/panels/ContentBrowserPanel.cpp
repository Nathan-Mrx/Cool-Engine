#include "../panels/ContentBrowserPanel.h"
#include "../../project/Project.h"
#include <imgui.h>

ContentBrowserPanel::ContentBrowserPanel() {}

void ContentBrowserPanel::OnImGuiRender() {
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
                ImGui::Button(filename.c_str(), { thumbnailSize, thumbnailSize });

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

    ImGui::Columns(1);
    ImGui::End();
}