#pragma once
#include <memory>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "../scene/Scene.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/HubPanel.h"
#include "../renderer/Framebuffer.h"
#include "../project/ProjectCompiler.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include "panels/MaterialEditorPanel.h"

struct EditorCamera {
    glm::vec3 Position = { -300.0f, 0.0f, 100.0f };
    glm::vec3 Front = { 1.0f, 0.0f, -0.2f };
    glm::vec3 WorldUp = { 0.0f, 0.0f, 1.0f };
    float Yaw = 0.0f;
    float Pitch = 0.0f;
};

enum class SceneState {
    Edit = 0,
    Play = 1,
    Pause = 2
};

enum class TabType {
    Scene,
    Material
};

struct EditorTab {
    std::string Name;
    std::filesystem::path Filepath;
    TabType Type = TabType::Scene; // Le type définit quels panneaux s'affichent !

    // Données spécifiques à la scène
    std::shared_ptr<Scene> SceneContext;
    bool IsPrefab = false;
};

class EditorLayer {
public:
    void OnAttach();
    void OnDetach();
    void OnUpdate(float ts);
    void OnImGuiRender();
    void CaptureViewportThumbnail(const std::string& projectPath);

private:
    void DrawMenuBar();
    void BeginDockspace();
    void EndDockspace();

    void UI_Toolbar();
    void OnScenePlay();
    void OnSceneStop();
    void OnScenePause();

    void OpenScene(const std::filesystem::path& path);
    void OpenPrefab(const std::filesystem::path& path);
    void CloseTab(int index);

private:
    std::shared_ptr<Scene> m_ActiveScene;

    SceneState m_SceneState = SceneState::Edit;

    EditorCamera m_EditorCamera;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    std::unique_ptr<ContentBrowserPanel> m_ContentBrowserPanel;
    HubPanel m_HubPanel;
    std::unique_ptr<Framebuffer> m_ViewportFramebuffer;
    bool m_ShowGrid = true;

    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
    glm::vec2 m_LastMousePosition = { 0.0f, 0.0f };

    int m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
    int m_GizmoMode = ImGuizmo::MODE::LOCAL;

    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;

    bool m_RequestCloseProject = false;
    void CloseProjectInternal();

    bool m_ShowProjectSettings = false;
    void DrawProjectSettings();

    void SaveScene();
    void SaveSceneAs();

    bool m_ShowCollisions = false;
    int m_RenderMode = 0; // 0: Lit, 1: Unlit, 2: Wireframe

    // --- NOUVEAU SYSTÈME D'ONGLETS ---
    std::vector<EditorTab> m_Tabs;
    int m_ActiveTabIndex = 0;
    bool m_ForceTabSelection = false;

    void DrawSplashScreen();
    uint32_t m_SplashTextureID = 0;
    std::filesystem::path m_PendingProjectPath;

    std::unique_ptr<MaterialEditorPanel> m_MaterialEditorPanel;
    bool m_ShowMaterialEditor = false;

    void OpenMaterial(const std::filesystem::path& path);

};