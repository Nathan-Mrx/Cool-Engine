#pragma once
#include <memory>
#include <string>
#include <glm/fwd.hpp>

#include "scene/Scene.h"

struct GLFWwindow;

// L'interface pure de notre moteur de rendu
class RendererAPI {
public:
    enum class API {
        None = 0, 
        OpenGL = 1, 
        Vulkan = 2
    };

    virtual ~RendererAPI() = default;

    // --- LE CONTRAT DE BASE ---
    // (Tu devras ajouter ici toutes les méthodes publiques de ton Renderer actuel
    // en les mettant en 'virtual ... = 0;')
    virtual void Init() = 0;
    virtual void Shutdown() = 0;
    
    virtual void Clear() = 0;
    virtual void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) = 0;
    virtual void RenderScene(Scene* scene, int renderMode) = 0;
    virtual void DrawGrid(bool enable) = 0;
    virtual void EndScene() = 0;
    virtual void SetShadowResolution(uint32_t resolution) = 0;
    
    // Exemples de getters qui étaient statiques
    virtual uint32_t GetIrradianceMapID() = 0;
    virtual uint32_t GetPrefilterMapID() = 0;
    virtual uint32_t GetBRDFLUTID() = 0;

    // --- GESTION DE L'API ---
    inline static API GetAPI() { return s_API; }
    static void SetAPI(API api) { s_API = api; }

    // --- NOUVEAU : CYCLE DE VIE IMGUI ---
    virtual void InitImGui(GLFWwindow* window) = 0;
    virtual void BeginImGuiFrame() = 0;
    virtual void EndImGuiFrame() = 0;
    virtual void ShutdownImGui() = 0;

    virtual void SubmitPushConstant(const glm::mat4& matrix) {} // Vide par défaut

private:
    static API s_API;
};