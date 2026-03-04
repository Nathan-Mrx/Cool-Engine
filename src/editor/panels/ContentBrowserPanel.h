#pragma once
#include <filesystem>
#include <functional> // Indispensable pour le callback

class ContentBrowserPanel {
public:
    ContentBrowserPanel();
    void OnImGuiRender();

    std::function<void(const std::filesystem::path&)> OnSceneOpenCallback;
    std::function<void(const std::filesystem::path&)> OnPrefabOpenCallback;

private:
    std::filesystem::path m_CurrentDirectory;
    std::filesystem::path m_BaseDirectory;
};