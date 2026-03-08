#pragma once
#include <glm/glm.hpp>
#include "../renderer/Shader.h"
#include "../scene/Scene.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "DDGIVolume.h"
#include "RendererAPI.h"
#include "editor/EditorLayer.h"

class OpenGLRenderer: public RendererAPI {
public:
    void Init() override;
    void Shutdown() override;
    void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos) override;
    void EndScene() override;

    void RenderScene(Scene* scene, int renderMode = 0) override;
    void DrawGrid(bool show) override;

    void DrawDebugBox(const glm::mat4& transform, const glm::vec3& color);
    void DrawDebugArrow(const glm::vec3& start, const glm::vec3& forward, const glm::vec3& right, const glm::vec3& up, const glm::vec3& color, const glm::mat4& view, const glm::mat4& projection, float length);

    void Clear() override;

    void BeginOutlineMask(const glm::mat4& transform);
    void BeginOutlineDraw(const glm::mat4& outlineTransform, const glm::vec3& color);
    void EndOutline();

    void SetShadowResolution(uint32_t resolution) override;

    uint32_t GetIrradianceMapID() override;


    uint32_t GetBRDFLUTID() override { return m_Data->BRDFLUTTexture; }
    uint32_t GetPrefilterMapID() override { return m_Data->PrefilterMap; }

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
        uint32_t EnvironmentMapID = 0;
        std::string CurrentSkyboxPath = "";

        // --- NOUVEAU : IBL BAKING ---
        std::unique_ptr<Shader> EquirectToCubeShader;
        std::unique_ptr<Shader> IrradianceShader;
        uint32_t EnvCubemap;
        uint32_t IrradianceMap;

        std::unique_ptr<Shader> PrefilterShader;
        std::unique_ptr<Shader> BRDFShader;
        uint32_t PrefilterMap = 0;
        uint32_t BRDFLUTTexture = 0;

        std::unique_ptr<DDGIVolume> GlobalDDGIVolume; // Ajoute ceci !
        std::unique_ptr<Shader> DDGIUpdateShader; // Le cerveau de notre GI
    };
    RendererData* m_Data;

    void UpdateSkybox(const std::string& hdrPath);

};
