#pragma once
#include <filesystem>

class ContentBrowserPanel {
public:
    ContentBrowserPanel();

    void OnImGuiRender();

private:
    std::filesystem::path m_CurrentDirectory;
};