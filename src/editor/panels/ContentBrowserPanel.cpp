#include "../panels/ContentBrowserPanel.h"

#include <fstream>
#include <vector>
#include <algorithm>

#include "../../project/Project.h"
#include <imgui.h>

#include "editor/AssetRegistry.h"
#include "renderer/TextureLoader.h"
#include "scene/Entity.h"
#include "scene/SceneSerializer.h"

#include "editor/EditorCommands.h"
#include "editor/UndoManager.h"
#include "renderer/RendererAPI.h"

// =========================================================================================
// COMMANDE D'UNDO POUR LE SYSTEME DE FICHIERS (Renommer & Déplacer)
// =========================================================================================
class FileSystemCommand : public IUndoableAction {
public:
    enum class ActionType { Rename, Create };

    FileSystemCommand(ActionType type, std::filesystem::path source, std::filesystem::path destination = "")
        : m_Type(type), m_Source(std::move(source)), m_Destination(std::move(destination)) {}

    void Undo() override {
        try {
            if (m_Type == ActionType::Rename) std::filesystem::rename(m_Destination, m_Source);
            else if (m_Type == ActionType::Create) std::filesystem::remove_all(m_Source);
        } catch(...) {}
    }

    void Redo() override {
        try {
            if (m_Type == ActionType::Rename) std::filesystem::rename(m_Source, m_Destination);
            else if (m_Type == ActionType::Create) {
                if (m_Source.extension().empty()) std::filesystem::create_directory(m_Source);
                else { std::ofstream f(m_Source); f.close(); }
            }
        } catch(...) {}
    }
private:
    ActionType m_Type;
    std::filesystem::path m_Source;
    std::filesystem::path m_Destination;
};


ContentBrowserPanel::ContentBrowserPanel() {}

void ContentBrowserPanel::OnImGuiRender() {
    ImGui::Begin("Content Browser");

    if (!Project::GetActive()) {
        ImGui::Text("No Project Active");
        ImGui::End();
        return;
    }

    if (m_CurrentDirectory.empty()) m_CurrentDirectory = Project::GetContentDirectory();

    // SÉCURITÉ : Vérification de l'API et pointeur nul
    if (m_DirectoryIcon == nullptr && std::filesystem::exists("icons/folder.png")) {
        m_DirectoryIcon = TextureLoader::LoadTexture("icons/folder.png");
    }

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2) && !m_SelectedPath.empty()) {
        m_IsRenaming = true;
        m_RenamingPath = m_SelectedPath;
        std::string name = std::filesystem::is_directory(m_SelectedPath) ? m_SelectedPath.filename().string() : m_SelectedPath.stem().string();
        snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
    }

    // --- LE NOUVEAU LAYOUT EN DEUX PANNEAUX ---
    if (ImGui::BeginTable("ContentBrowserTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        // PANNEAU DE GAUCHE : L'arborescence
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("FolderTree");
        DrawDirectoryTree(Project::GetContentDirectory());
        ImGui::EndChild();

        // PANNEAU DE DROITE : La grille de contenu
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("FolderContent");
        DrawTopBar();
        DrawContentGrid();
        ImGui::EndChild();

        ImGui::EndTable();
    }

    HandleBackgroundContextMenu();
    DrawCreateAssetPopup();
    DrawMovePopup(); // Le popup modal est dessiné par-dessus tout

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        m_SelectedPath = "";
        m_IsRenaming = false;
    }

    ImGui::End();
}

void ContentBrowserPanel::DrawDirectoryTree(const std::filesystem::path& directoryPath) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_CurrentDirectory == directoryPath) flags |= ImGuiTreeNodeFlags_Selected;

    std::string filename = directoryPath.filename().string();
    if (directoryPath == Project::GetContentDirectory()) filename = "Content";

    // CORRECTION 1 : On utilise le texte explicite comme ID (pas de cast en void*)
    std::string pathStr = directoryPath.string();
    bool opened = ImGui::TreeNodeEx(pathStr.c_str(), flags, "%s", filename.c_str());

    if (ImGui::IsItemClicked()) {
        m_CurrentDirectory = directoryPath;
        m_SelectedPath = "";
    }

    // Permet de glisser un objet DEPUIS la grille VERS ce noeud de l'arbre
    HandleDragDropOnDirectory(directoryPath);

    if (opened) {
        // CORRECTION 2 : On récupère et on trie les dossiers par ordre alphabétique
        std::vector<std::filesystem::path> subDirs;
        for (auto& directoryEntry : std::filesystem::directory_iterator(directoryPath)) {
            if (directoryEntry.is_directory()) {
                subDirs.push_back(directoryEntry.path());
            }
        }

        std::sort(subDirs.begin(), subDirs.end());

        for (const auto& subDir : subDirs) {
            DrawDirectoryTree(subDir);
        }
        ImGui::TreePop();
    }
}

void ContentBrowserPanel::HandleDragDropOnDirectory(const std::filesystem::path& targetPath) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::filesystem::path itemPath = (const char*)payload->Data;

            // Sécurité : On ne déplace pas un dossier dans lui-même, ni dans son propre sous-dossier
            if (itemPath != targetPath && targetPath.string().find(itemPath.string()) != 0) {
                m_ItemToMove = itemPath;
                m_MoveDestination = targetPath;
                m_OpenMovePopup = true; // Ouvre le popup !
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void ContentBrowserPanel::DrawMovePopup() {
    if (m_OpenMovePopup) {
        ImGui::OpenPopup("Move Asset");
        m_OpenMovePopup = false;
    }

    // Force la fenêtre à apparaître exactement sous la souris lors de son ouverture
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);

    // On utilise un simple "BeginPopup" sans "Modal" pour retirer le fond bloquant !
    if (ImGui::BeginPopup("Move Asset")) {
        ImGui::Text("Voulez-vous deplacer :\n'%s'\nvers le dossier :\n'%s' ?",
            m_ItemToMove.filename().string().c_str(),
            m_MoveDestination.filename().string().c_str());
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        if (ImGui::Button("Deplacer", ImVec2(120, 0))) {
            std::filesystem::path newPath = m_MoveDestination / m_ItemToMove.filename();
            if (!std::filesystem::exists(newPath)) {
                try {
                    std::filesystem::rename(m_ItemToMove, newPath);
                    UndoManager::BeginTransaction("Move Asset");
                    UndoManager::PushAction(std::make_unique<FileSystemCommand>(FileSystemCommand::ActionType::Rename, m_ItemToMove, newPath));
                    UndoManager::EndTransaction();

                    if (m_SelectedPath == m_ItemToMove) m_SelectedPath = newPath;
                } catch (...) {}
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Annuler", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::DrawTopBar() {
    std::filesystem::path contentDir = Project::GetContentDirectory();

    if (m_CurrentDirectory != contentDir) {
        if (ImGui::Button("<-")) m_CurrentDirectory = m_CurrentDirectory.parent_path();
        ImGui::SameLine();
    }

    if (ImGui::Button("New Folder")) ImGui::OpenPopup("NewFolderPopup");

    if (ImGui::BeginPopup("NewFolderPopup")) {
        static char folderName[128] = "NewFolder";
        ImGui::InputText("##Name", folderName, sizeof(folderName));
        if (ImGui::Button("Create")) {
            std::filesystem::path newPath = m_CurrentDirectory / folderName;
            if (!std::filesystem::exists(newPath)) {
                std::filesystem::create_directory(newPath);
                UndoManager::BeginTransaction("Create Folder");
                UndoManager::PushAction(std::make_unique<FileSystemCommand>(FileSystemCommand::ActionType::Create, newPath));
                UndoManager::EndTransaction();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    std::filesystem::path relativePath = std::filesystem::relative(m_CurrentDirectory, Project::GetProjectDirectory());
    ImGui::Text("%s", relativePath.string().c_str());
    ImGui::Separator();
}

void ContentBrowserPanel::DrawContentGrid() {
    if (!std::filesystem::exists(m_CurrentDirectory)) return;

    // --- CORRECTION : Appliquer l'échelle aux valeurs en dur ---
    float scale = ImGui::GetIO().FontGlobalScale;
    float scaledThumbnail = m_ThumbnailSize * scale;
    float scaledPadding = m_Padding * scale;

    float cellSize = scaledThumbnail + scaledPadding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    std::vector<std::filesystem::directory_entry> entries;
    for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory)) entries.push_back(directoryEntry);

    std::sort(entries.begin(), entries.end(), [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b) {
        bool aIsDir = a.is_directory();
        bool bIsDir = b.is_directory();
        if (aIsDir && !bIsDir) return true;
        if (!aIsDir && bIsDir) return false;
        if (!aIsDir && !bIsDir) {
            std::string extA = a.path().extension().string();
            std::string extB = b.path().extension().string();
            if (extA != extB) return extA < extB;
        }
        return a.path().filename().string() < b.path().filename().string();
    });

    for (auto& directoryEntry : entries) {
        const auto& path = directoryEntry.path();
        std::string filename = path.filename().string();
        std::string displayName = directoryEntry.is_directory() ? filename : path.stem().string();

        ImGui::PushID(filename.c_str());

        // --- CORRECTION : Utiliser scaledThumbnail au lieu de m_ThumbnailSize ---
        if (directoryEntry.is_directory()) DrawDirectoryEntry(directoryEntry, scaledThumbnail);
        else DrawFileEntry(directoryEntry, scaledThumbnail);

        bool isSelected = (m_SelectedPath == path);
        DrawItemLabelOrRename(directoryEntry, displayName, scaledThumbnail, isSelected);

        ImGui::NextColumn();
        ImGui::PopID();
    }

    ImGui::Columns(1);
}

void ContentBrowserPanel::DrawDirectoryEntry(const std::filesystem::directory_entry& entry, float thumbnailSize) {
    ImVec2 uv0 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(0, 1) : ImVec2(0, 0);
    ImVec2 uv1 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(1, 0) : ImVec2(1, 1);

    const auto& path = entry.path();
    bool isSelected = (m_SelectedPath == path);

    if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    if (m_DirectoryIcon != nullptr) {
        ImGui::ImageButton("##Dir", (ImTextureID)m_DirectoryIcon, { thumbnailSize, thumbnailSize }, uv0, uv1, ImVec4(0,0,0,0));
    } else {
        ImGui::Button("DIR", { thumbnailSize, thumbnailSize });
    }

    if (isSelected) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) { m_SelectedPath = path; m_IsRenaming = false; }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { m_CurrentDirectory /= path.filename(); m_SelectedPath = ""; }

    if (ImGui::BeginDragDropSource()) {
        std::string itemPath = path.string();
        ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
        ImGui::Text("Move %s", path.filename().string().c_str());
        ImGui::EndDragDropSource();
    }

    HandleDragDropOnDirectory(path);
}

void ContentBrowserPanel::DrawFileEntry(const std::filesystem::directory_entry& entry, float thumbnailSize) {
    ImVec2 uv0 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(0, 1) : ImVec2(0, 0);
    ImVec2 uv1 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(1, 0) : ImVec2(1, 1);

    const auto& path = entry.path();
    bool isSelected = (m_SelectedPath == path);
    std::string filename = path.filename().string();

    AssetTypeInfo info;
    bool isKnownAsset = AssetRegistry::GetInfo(path.extension().string(), info);
    ImVec4 bgCol = isKnownAsset ? ImVec4(info.Color.x * 0.4f, info.Color.y * 0.4f, info.Color.z * 0.4f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

    if (isSelected) { bgCol.x = std::min(1.0f, bgCol.x + 0.3f); bgCol.y = std::min(1.0f, bgCol.y + 0.3f); bgCol.z = std::min(1.0f, bgCol.z + 0.3f); }

    ImGui::PushStyleColor(ImGuiCol_Button, bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bgCol.x * 1.5f, bgCol.y * 1.5f, bgCol.z * 1.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(bgCol.x * 0.8f, bgCol.y * 0.8f, bgCol.z * 0.8f, 1.0f));

    if (isKnownAsset && info.IconID != nullptr) {
        ImGui::ImageButton("##Asset", (ImTextureID)(uintptr_t)info.IconID, { thumbnailSize, thumbnailSize }, uv0, uv1, bgCol);
    } else {
        ImGui::Button(path.extension().string().c_str(), { thumbnailSize, thumbnailSize });
    }

    ImGui::PopStyleColor(3);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) { m_SelectedPath = path; m_IsRenaming = false; }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (path.extension() == ".cescene" && OnSceneOpenCallback) OnSceneOpenCallback(path);
        else if (path.extension() == ".ceprefab" && OnPrefabOpenCallback) OnPrefabOpenCallback(path);
        else if (path.extension() == ".cemat" && OnMaterialOpenCallback) OnMaterialOpenCallback(path);
        else if (path.extension() == ".cematinst" && OnMaterialInstanceOpenCallback) OnMaterialInstanceOpenCallback(path);
    }

    if (ImGui::BeginDragDropSource()) {
        std::string itemPath = path.string();
        ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
        ImGui::Text("Move %s", filename.c_str());
        ImGui::EndDragDropSource();
    }
}

void ContentBrowserPanel::DrawItemLabelOrRename(const std::filesystem::directory_entry& entry, const std::string& displayName, float thumbnailSize, bool isSelected) {
    const auto& path = entry.path();

    if (m_IsRenaming && m_RenamingPath == path) {
        ImGui::PushItemWidth(thumbnailSize);
        if (ImGui::InputText("##Rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            std::string ext = entry.is_directory() ? "" : path.extension().string();
            std::filesystem::path newPath = path.parent_path() / (std::string(m_RenameBuffer) + ext);

            if (!std::filesystem::exists(newPath) && strlen(m_RenameBuffer) > 0) {
                try {
                    std::filesystem::rename(path, newPath);
                    UndoManager::BeginTransaction("Rename Asset");
                    UndoManager::PushAction(std::make_unique<FileSystemCommand>(FileSystemCommand::ActionType::Rename, path, newPath));
                    UndoManager::EndTransaction();
                    m_SelectedPath = newPath;
                } catch (...) {}
            }
            m_IsRenaming = false;
        }

        if (!ImGui::IsItemActive() && ImGui::IsWindowFocused()) ImGui::SetKeyboardFocusHere(-1);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered()) m_IsRenaming = false;

        ImGui::PopItemWidth();
    } else {
        ImGui::TextWrapped("%s", displayName.c_str());

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (isSelected) {
                m_IsRenaming = true;
                m_RenamingPath = path;
                snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", displayName.c_str());
            } else {
                m_SelectedPath = path;
                m_IsRenaming = false;
            }
        }
    }
}

void ContentBrowserPanel::HandleBackgroundContextMenu() {
    if (ImGui::BeginPopupContextWindow("ContentBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("Create...")) {
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
}

void ContentBrowserPanel::DrawCreateAssetPopup() {
    if (m_OpenCreateAssetPopup) { ImGui::OpenPopup("Name New Asset"); m_OpenCreateAssetPopup = false; }

    if (ImGui::BeginPopupModal("Name New Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter %s name:", m_CreateAssetType.c_str());
        ImGui::InputText("##AssetName", m_NewAssetName, sizeof(m_NewAssetName));
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            std::string nameStr = m_NewAssetName;
            if (!nameStr.empty()) {
                std::filesystem::path newAssetPath = m_CurrentDirectory / (nameStr + m_CreateAssetExtension);
                if (!std::filesystem::exists(newAssetPath)) {
                    AssetTypeInfo info;
                    if (AssetRegistry::GetInfo(m_CreateAssetExtension, info) && info.Instance) info.Instance->CreateDefaultAsset(newAssetPath);
                    else { std::ofstream file(newAssetPath); file.close(); }

                    UndoManager::BeginTransaction("Create Asset");
                    UndoManager::PushAction(std::make_unique<FileSystemCommand>(FileSystemCommand::ActionType::Create, newAssetPath));
                    UndoManager::EndTransaction();
                }
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}