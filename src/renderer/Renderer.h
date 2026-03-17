#pragma once
#include "RendererAPI.h"
#include <memory>
#include <string>
#include <glm/glm.hpp>

class Scene;

class Renderer {
public:
    static void Init();
    static void Shutdown();

    // --- REDIRECTIONS (Le pont entre le jeu et le GPU) ---
    static void Clear(Scene* scene = nullptr) { s_Instance->Clear(scene); }

    static void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
        s_Instance->BeginScene(view, projection, cameraPos);
    }

    static void RenderScene(Scene* scene, int renderMode) {
        s_Instance->RenderScene(scene, renderMode);
    }

    static void DrawGrid(bool enable) {
        s_Instance->DrawGrid(enable);
    }

    static void EndScene() {
        s_Instance->EndScene();
    }

    static void SetShadowResolution(uint32_t resolution) {
        s_Instance->SetShadowResolution(resolution);
    }

    // ... tes getters IBL ...
    static uint32_t GetIrradianceMapID() { return s_Instance->GetIrradianceMapID(); }
    static uint32_t GetPrefilterMapID() { return s_Instance->GetPrefilterMapID(); }
    static uint32_t GetBRDFLUTID() { return s_Instance->GetBRDFLUTID(); }

    inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

    // --- NOUVEAU : REDIRECTIONS IMGUI ---
    static void InitImGui(GLFWwindow* window) { s_Instance->InitImGui(window); }
    static void BeginImGuiFrame() { s_Instance->BeginImGuiFrame(); }
    static void EndImGuiFrame() { s_Instance->EndImGuiFrame(); }
    static void ShutdownImGui() { s_Instance->ShutdownImGui(); }

    static void SubmitPushConstant(const glm::mat4& matrix) { s_Instance->SubmitPushConstant(matrix); }

private:
    static std::unique_ptr<RendererAPI> s_Instance;
};