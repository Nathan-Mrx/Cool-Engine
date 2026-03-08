#include "Renderer.h"
#include "OpenGLRenderer.h"
// #include "VulkanRenderer.h" // Pour plus tard !

std::unique_ptr<RendererAPI> Renderer::s_Instance = nullptr;

void Renderer::Init() {
    // C'est ici qu'a lieu la magie du choix de l'API !
    switch (RendererAPI::GetAPI()) {
    case RendererAPI::API::OpenGL:
        s_Instance = std::make_unique<OpenGLRenderer>();
        break;
    case RendererAPI::API::Vulkan:
        // s_Instance = std::make_unique<VulkanRenderer>();
        break;
    case RendererAPI::API::None:
    default:
        // Crash ou erreur
        break;
    }

    s_Instance->Init();
}

void Renderer::Shutdown() {
    s_Instance->Shutdown();
    s_Instance.reset();
}