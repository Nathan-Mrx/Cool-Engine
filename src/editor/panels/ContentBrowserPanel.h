#pragma once
#include <filesystem>
#include <functional>
#include <string>

class ContentBrowserPanel {
public:
    ContentBrowserPanel();

    void OnImGuiRender();

    void SetSceneOpenCallback(const std::function<void(const std::filesystem::path&)>& cb) { OnSceneOpenCallback = cb; }
    void SetPrefabOpenCallback(const std::function<void(const std::filesystem::path&)>& cb) { OnPrefabOpenCallback = cb; }
    void SetMaterialOpenCallback(const std::function<void(const std::filesystem::path&)>& cb) { OnMaterialOpenCallback = cb; }
    void SetMaterialInstanceOpenCallback(const std::function<void(const std::filesystem::path&)>& cb) { OnMaterialInstanceOpenCallback = cb; }

private:
    void DrawTopBar();
    void DrawContentGrid();

    // --- NOUVELLES FONCTIONS ---
    void DrawDirectoryTree(const std::filesystem::path& directoryPath);
    void HandleDragDropOnDirectory(const std::filesystem::path& targetPath);
    void DrawMovePopup();

    void DrawDirectoryEntry(const std::filesystem::directory_entry& entry, float thumbnailSize);
    void DrawFileEntry(const std::filesystem::directory_entry& entry, float thumbnailSize);
    void DrawItemLabelOrRename(const std::filesystem::directory_entry& entry, const std::string& displayName, float thumbnailSize, bool isSelected);
    void HandleBackgroundContextMenu();
    void DrawCreateAssetPopup();

private:
    std::filesystem::path m_CurrentDirectory;
    std::filesystem::path m_SelectedPath;

    float m_ThumbnailSize = 90.0f;
    float m_Padding = 16.0f;
    void* m_DirectoryIcon = nullptr;

    bool m_IsRenaming = false;
    std::filesystem::path m_RenamingPath;
    char m_RenameBuffer[256] = {0};

    bool m_OpenCreateAssetPopup = false;
    std::string m_CreateAssetType;
    std::string m_CreateAssetExtension;
    char m_NewAssetName[256] = "NewAsset";

    // --- VARIABLES POUR LE DÉPLACEMENT ---
    std::filesystem::path m_ItemToMove;
    std::filesystem::path m_MoveDestination;
    bool m_OpenMovePopup = false;

    std::function<void(const std::filesystem::path&)> OnSceneOpenCallback;
    std::function<void(const std::filesystem::path&)> OnPrefabOpenCallback;
    std::function<void(const std::filesystem::path&)> OnMaterialOpenCallback;
    std::function<void(const std::filesystem::path&)> OnMaterialInstanceOpenCallback;
};