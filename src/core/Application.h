#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>

#include "../renderer/Shader.h"
#include "../renderer/Framebuffer.h"

class Application {
public:
    Application(const std::string& name = "Mon Moteur 3D", int width = 1280, int height = 720);
    ~Application();

    void Run();

private:
    void BeginImGui();
    void EndImGui();
    void Shutdown();

private:
    GLFWwindow* m_Window;
    bool m_Running = true;

    // Temps
    float m_DeltaTime = 0.0f;
    float m_LastFrameTime = 0.0f;

    // Rendu & OpenGL
    uint32_t m_VAO, m_VBO;
    std::unique_ptr<Shader> m_Shader;
    std::unique_ptr<Framebuffer> m_Framebuffer;

    // ECS (EnTT)
    entt::registry m_Registry;
    entt::entity m_TriangleEntity;
    entt::entity m_CameraEntity;

    // État de l'Éditeur
    entt::entity m_SelectedContext = entt::null;
    bool m_ViewportHovered = false;
    glm::vec2 m_LastMousePosition = { 0.0f, 0.0f };
};