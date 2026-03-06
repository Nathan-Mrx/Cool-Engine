#pragma once
#include <filesystem>
#include <functional>
#include <string>

class ContentBrowserPanel {
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

    // --- NOUVEAU : La machine à créer des Assets ---
    void DrawCreateAssetPopup();

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
};