#pragma once
#include <memory>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "../scene/Scene.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/HubPanel.h"
#include "../renderer/Framebuffer.h"

struct EditorCamera {
    glm::vec3 Position = { -300.0f, 0.0f, 100.0f };
    glm::vec3 Front = { 1.0f, 0.0f, -0.2f };
    glm::vec3 WorldUp = { 0.0f, 0.0f, 1.0f };
    float Yaw = 0.0f;
    float Pitch = 0.0f;
};

class EditorLayer {
public:
    void OnAttach();
    void OnDetach();
    void OnUpdate(float ts);
    void OnImGuiRender();

private:
    void DrawMenuBar();
    void BeginDockspace();
    void EndDockspace();

private:
    std::shared_ptr<Scene> m_ActiveScene;
    EditorCamera m_EditorCamera;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    std::unique_ptr<ContentBrowserPanel> m_ContentBrowserPanel;
    HubPanel m_HubPanel;
    std::unique_ptr<Framebuffer> m_ViewportFramebuffer;
    bool m_ShowGrid = true;

    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
    glm::vec2 m_LastMousePosition = { 0.0f, 0.0f };
};