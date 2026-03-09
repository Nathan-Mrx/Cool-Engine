#include "Application.h"
#include "Input.h"
#include "../renderer/Renderer.h"
#include "../editor/EditorLayer.h"
#include <glad/glad.h>
#include <nfd.hpp>
#include <iostream>
#include <stb_image.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_vulkan.h> // Indispensable pour l'architecture Vulkan

#include "editor/UITheme.h"
#include "physics/PhysicsEngine.h"
#include <fstream>
#include <nlohmann/json.hpp>

#include "renderer/VulkanRenderer.h"

Application* Application::s_Instance = nullptr;

Application::Application(const std::string& name, int width, int height) {
    s_Instance = this;

    // --- DIAGNOSTIC GLFW ---
    glfwSetErrorCallback([](int error, const char* description) {
        std::cerr << "[GLFW Error " << error << "] " << description << "\n";
    });

    // ==========================================
    // 1. SYSTÈME ET FENÊTRE
    // ==========================================
    if (!glfwInit()) {
        std::cerr << "Erreur: Impossible d'initialiser GLFW!\n";
        return;
    }

    bool maximized = false;
    std::ifstream file("editor_preferences.json");
    if (file.is_open()) {
        nlohmann::json data;
        try {
            file >> data;
            width = data.value("WindowWidth", width);
            height = data.value("WindowHeight", height);
            maximized = data.value("WindowMaximized", false);
        } catch(...) {}
        file.close();
    }

    // Configuration de la fenêtre selon l'API sélectionnée
    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    } else {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    m_Window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
    if (maximized) glfwMaximizeWindow(m_Window);

    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        glfwMakeContextCurrent(m_Window);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize OpenGL loader!\n";
        }
        glfwSwapInterval(1);
    }

    // ==========================================
    // 2. INITIALISATION MOTEUR GRAPHIQUE
    // Doit TOUJOURS être appelé avant NFD/ImGui sur Linux (Sécurité Wayland)
    // ==========================================
    Renderer::Init();
    PhysicsEngine::Init();

    // ==========================================
    // 3. SYSTÈMES SECONDAIRES
    // ==========================================
    Input::Init(m_Window);
    NFD::Init();

    // ==========================================
    // 4. IMGUI : CONTEXTE DE BASE
    // ==========================================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // ==========================================
    // 5. IMGUI : LIAISON API GRAPHIQUE
    // ==========================================
    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
        ImGui_ImplOpenGL3_Init("#version 460");
    }
    else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        Renderer::InitImGui(m_Window);
    }

    // ==========================================
    // 6. THÈME VISUEL ET POLICES
    // ==========================================
    UITheme::Apply();
    UITheme::LoadFonts();

    // ==========================================
    // 7. L'ÉDITEUR
    // ==========================================
    m_EditorLayer = std::make_unique<EditorLayer>();
    m_EditorLayer->OnAttach();
}

Application::~Application() {
    // 1. Détacher l'éditeur proprement
    if (m_EditorLayer) {
        m_EditorLayer->OnDetach();
        m_EditorLayer.reset();
    }

    // 2. Éteindre ImGui
    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        Renderer::ShutdownImGui();
    }
    ImGui::DestroyContext();

    // 3. Éteindre les sous-systèmes
    NFD::Quit();
    PhysicsEngine::Shutdown();
    Renderer::Shutdown();

    // 4. Détruire la fenêtre
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Application::Run() {
    while (!glfwWindowShouldClose(m_Window)) {
        float time = (float)glfwGetTime();
        m_DeltaTime = time - m_LastFrameTime;
        m_LastFrameTime = time;

        glfwPollEvents();

        // ==========================================
        // --- BOUCLE DE RENDU OPENGL ---
        // ==========================================
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {

            Renderer::Clear();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (m_EditorLayer) {
                m_EditorLayer->OnUpdate(m_DeltaTime);
                m_EditorLayer->OnImGuiRender();
            }

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                GLFWwindow* backup_current_context = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(backup_current_context);
            }

            glfwSwapBuffers(m_Window);
        }

        // ==========================================
        // --- BOUCLE DE RENDU VULKAN ---
        // ==========================================
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {

            // 1. MISE À JOUR ET RENDU HORS-ÉCRAN (Scènes 3D)
            // C'est ici que les Framebuffers s'activent de manière isolée
            if (m_EditorLayer) {
                m_EditorLayer->OnUpdate(m_DeltaTime);
            }

            // 2. RENDU PRINCIPAL (Swapchain & ImGui)
            Renderer::Clear(); // Commence le carnet de la fenêtre
            Renderer::BeginImGuiFrame();

            if (m_EditorLayer) {
                m_EditorLayer->OnImGuiRender(); // Dessine l'interface par-dessus
            }

            Renderer::EndImGuiFrame();
            Renderer::EndScene(); // Soumet le carnet complet au GPU

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
    }

    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        vkDeviceWaitIdle(VulkanRenderer::Get()->GetDevice());
    }
}

void Application::SetWindowIcon(const std::string& path) {
    GLFWimage images[1];
    int channels;
    images[0].pixels = stbi_load(path.c_str(), &images[0].width, &images[0].height, &channels, 4);

    if (images[0].pixels) {
        glfwSetWindowIcon(m_Window, 1, images);
        stbi_image_free(images[0].pixels);
    }
}