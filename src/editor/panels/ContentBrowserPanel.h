#pragma once
#include <filesystem>
#include <functional>
#include <string>

class ContentBrowserPanel
{
public:
    ContentBrowserPanel();
    void OnImGuiRender();

    std::function<void(const std::filesystem::path&)> OnSceneOpenCallback;
    std::function<void(const std::filesystem::path&)> OnPrefabOpenCallback;
    std::function<void(const std::filesystem::path&)> OnMaterialOpenCallback;
    std::function<void(const std::filesystem::path&)> OnMaterialInstanceOpenCallback;

private:
    std::filesystem::path m_CurrentDirectory;
    std::filesystem::path m_BaseDirectory;

    // --- SOUS-FONCTIONS DE RENDU (Refactoring) ---
    void DrawTopBar();
    void DrawContentGrid();
    void DrawDirectoryEntry(const std::filesystem::directory_entry& entry, float thumbnailSize);
    void DrawFileEntry(const std::filesystem::directory_entry& entry, float thumbnailSize);
    void DrawItemLabelOrRename(const std::filesystem::directory_entry& entry, const std::string& displayName, float thumbnailSize, bool isSelected);
    void HandleBackgroundContextMenu();

    void DrawCreateAssetPopup();

private:
    bool m_OpenCreateAssetPopup = false;
    std::string m_CreateAssetType = "";      // "Material", "Prefab", etc.
    std::string m_CreateAssetExtension = ""; // ".cemat", ".ceprefab", etc.
    char m_NewAssetName[128] = "";

    uint32_t m_DirectoryIcon = 0;

    // --- ÉTAT DE SÉLECTION ET RENOMMAGE ---
    std::filesystem::path m_SelectedPath;
    std::filesystem::path m_RenamingPath;
    bool m_IsRenaming = false;
    char m_RenameBuffer[256] = "";

    // --- PRÉFÉRENCES UI ---
    float m_ThumbnailSize = 90.0f;
    float m_Padding = 16.0f;
};