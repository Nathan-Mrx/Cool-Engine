#include "ContentBrowserPanel.h"
#include "../project/Project.h"
#include <imgui.h>

ContentBrowserPanel::ContentBrowserPanel() {
}

void ContentBrowserPanel::OnImGuiRender() {
    auto activeProject = Project::GetActive();
    if (!activeProject) return; // Ne rien faire si aucun projet n'est chargé

    ImGui::Begin("Content Browser");

    const auto& contentRoot = Project::GetContentDirectory();

    // Initialisation du chemin lors du premier chargement de projet
    if (m_CurrentDirectory.empty()) {
        m_CurrentDirectory = contentRoot;
    }

    // --- Barre de Navigation (Breadcrumbs) ---
    if (m_CurrentDirectory != contentRoot) {
        if (ImGui::Button("<- Back")) {
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
        }
    }

    // Calcul de l'affichage en grille (Grid Layout)
    static float padding = 16.0f;
    static float thumbnailSize = 64.0f;
    float cellSize = thumbnailSize + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
        const auto& path = directoryEntry.path();
        auto relativePath = std::filesystem::relative(path, contentRoot);
        std::string filenameString = relativePath.filename().string();

        ImGui::PushID(filenameString.c_str());
        
        // Icône temporaire (en attendant ton système de textures)
        const char* icon = directoryEntry.is_directory() ? "[DIR]" : "[FILE]";
        
        ImGui::Button(icon, { thumbnailSize, thumbnailSize });

        // --- SOURCE DU DRAG & DROP ---
        if (ImGui::BeginDragDropSource()) {
            // Sur Linux, c_str() renvoie un const char*
            const char* itemPath = relativePath.c_str();

            // On utilise strlen pour calculer la taille du payload
            ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath, (strlen(itemPath) + 1) * sizeof(char));

            ImGui::Text("%s", filenameString.c_str());
            ImGui::EndDragDropSource();
        }

        // Navigation au double-clic
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (directoryEntry.is_directory())
                m_CurrentDirectory /= path.filename();
        }

        ImGui::TextWrapped("%s", filenameString.c_str());

        ImGui::NextColumn();
        ImGui::PopID();
    }

    ImGui::Columns(1);
    ImGui::End();
}