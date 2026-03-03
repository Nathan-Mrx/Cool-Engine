#pragma once
#include <glm/glm.hpp>
#include "../renderer/Shader.h"
#include "../scene/Scene.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "editor/EditorLayer.h"

class Renderer {
public:
    static void Init();
    static void Shutdown();
    static void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos);
    static void EndScene();

    static void RenderScene(Scene* scene);
    static void DrawGrid(bool show);
    static void DrawDebugArrow(const glm::vec3& start, const glm::vec3& forward, const glm::vec3& right, const glm::vec3& up, const glm::vec3& color, const glm::mat4& view, const glm::mat4& projection, float length);
    static void Clear();

private:
    struct RendererData {
        std::unique_ptr<Shader> MainShader;
        std::unique_ptr<Shader> GridShader;
        std::unique_ptr<Shader> LineShader; // For debug Lines
        uint32_t GridVAO, GridVBO;

        // Ajoute ces deux lignes pour stocker l'état de la caméra
        glm::mat4 CurrentView;
        glm::mat4 CurrentProjection;
    };
    static RendererData* s_Data;
};
