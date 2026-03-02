#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <entt/entt.hpp>

#include "../renderer/Shader.h"
#include "../ecs/Components.h"
#include "renderer/Framebuffer.h"

class Application {
public:
    Application(const std::string& name = "Game Engine", int width = 800, int height = 600);
    ~Application();

    void Run();
    void Close();

    // Un getter propre pour que le reste du moteur puisse y accéder
    float GetDeltaTime() const { return m_DeltaTime; }

private:
    void Init();
    void Shutdown();

    // On sépare la logique de rendu ImGui pour plus de clarté
    void BeginImGui();
    void EndImGui();

private:
    GLFWwindow* m_Window;
    bool m_Running = true;
    float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

    std::unique_ptr<Shader> m_Shader;
    unsigned int m_VAO, m_VBO;

    entt::registry m_Registry;       // La base de données de l'ECS
    entt::entity m_TriangleEntity;   // L'ID de notre entité
    entt::entity m_CameraEntity;     // L'ID de la caméra

    // --- TEMPS ---
    float m_DeltaTime = 0.0f;     // Temps écoulé entre la frame actuelle et la précédente
    float m_LastFrameTime = 0.0f; // Temps de la frame précédente

    std::unique_ptr<Framebuffer> m_Framebuffer;

    bool m_ViewportHovered = false;

    glm::vec2 m_LastMousePosition = { 0.0f, 0.0f };

    entt::entity m_SelectedContext = entt::null; // L'entité actuellement sélectionnée
};
