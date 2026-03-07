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

    static void RenderScene(Scene* scene, int renderMode = 0);
    static void DrawGrid(bool show);

    static void DrawDebugBox(const glm::mat4& transform, const glm::vec3& color);
    static void DrawDebugArrow(const glm::vec3& start, const glm::vec3& forward, const glm::vec3& right, const glm::vec3& up, const glm::vec3& color, const glm::mat4& view, const glm::mat4& projection, float length);

    static void Clear();

    static void BeginOutlineMask(const glm::mat4& transform);
    static void BeginOutlineDraw(const glm::mat4& outlineTransform, const glm::vec3& color);
    static void EndOutline();

    static void SetShadowResolution(uint32_t resolution);

    static uint32_t GetIrradianceMapID();

private:
    struct RendererData {
        std::unique_ptr<Shader> MainShader;
        std::unique_ptr<Shader> GridShader;
        std::unique_ptr<Shader> LineShader;
        std::unique_ptr<Shader> OutlineShader;

        // --- NOUVEAU : Le matériel pour les ombres ---
        std::unique_ptr<Shader> ShadowShader;
        std::unique_ptr<Framebuffer> ShadowFramebuffer;

        uint32_t GridVAO, GridVBO;
        uint32_t DebugBoxVAO, DebugBoxVBO;

        glm::mat4 CurrentView;
        glm::mat4 CurrentProjection;

        // --- SKYBOX ---
        std::unique_ptr<Shader> SkyboxShader;
        uint32_t SkyboxVAO, SkyboxVBO;
        uint32_t EnvironmentMapID;

        // --- NOUVEAU : IBL BAKING ---
        std::unique_ptr<Shader> EquirectToCubeShader;
        std::unique_ptr<Shader> IrradianceShader;
        uint32_t EnvCubemap;
        uint32_t IrradianceMap;
    };
    static RendererData* s_Data;

    static void SetupIBL(); // Fonction interne
};
