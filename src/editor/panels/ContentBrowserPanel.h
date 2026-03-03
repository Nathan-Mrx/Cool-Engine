#pragma once
#include <filesystem>

class ContentBrowserPanel {
public:
    ContentBrowserPanel();
    void OnImGuiRender();

private:
    std::filesystem::path m_CurrentDirectory;
    // On garde une référence à la racine pour ne pas "remonter" trop haut
    std::filesystem::path m_BaseDirectory;
};