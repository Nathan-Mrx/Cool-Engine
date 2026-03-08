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
// (Plus tard, on ajoutera #include <imgui_impl_vulkan.h> ici)

#include "editor/UITheme.h"
#include "physics/PhysicsEngine.h"
#include <fstream>
#include <nlohmann/json.hpp>


Application* Application::s_Instance = nullptr;

Application::Application(const std::string& name, int width, int height) {
    s_Instance = this;

    // 1. SYSTÈME ET FENÊTRE
    if (!glfwInit()) {
        std::cout << "Erreur: Impossible d'initialiser GLFW!\n";
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

    // Le paramètre crucial
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
            std::cout << "Failed to initialize OpenGL loader!\n";
        }
        glfwSwapInterval(1);
    }

    // ==========================================================
    // --- LE FIX EST ICI : ON ALLUME VULKAN IMMÉDIATEMENT ---
    // Avant ImGui, avant les polices, avant NFD. Aucune corruption possible !
    // ==========================================================
    Renderer::Init();
    PhysicsEngine::Init();

    // 2. SYSTÈMES SECONDAIRES
    Input::Init(m_Window);
    NFD::Init(); // Maintenant que Vulkan a sa surface, GTK/Wayland peut s'allumer

    // 3. IMGUI : CRÉATION ET LIAISON
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
        ImGui_ImplOpenGL3_Init("#version 460");
    }
    else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        Renderer::InitImGui(m_Window);
    }

    // 4. THÈME VISUEL (En tout dernier, car il interagit avec ImGui qui est maintenant lié !)
    UITheme::Apply();
    UITheme::LoadFonts();
}
Application::~Application() {
    if (m_EditorLayer) {
        m_EditorLayer->OnDetach();
    }

    PhysicsEngine::Shutdown();

    // --- SHUTDOWN IMGUI SELON L'API ---
    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        Renderer::ShutdownImGui();
    }
    ImGui::DestroyContext();

    Renderer::Shutdown();
    NFD::Quit();
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Application::Run() {
    while (m_Running && !glfwWindowShouldClose(m_Window)) {
        float time = (float)glfwGetTime();
        m_DeltaTime = time - m_LastFrameTime;
        m_LastFrameTime = time;

        // Indispensable pour garder la fenêtre réactive et lire les inputs
        glfwPollEvents();

        // --- BOUCLE DE RENDU OPENGL ---
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {

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
        // --- BOUCLE DE RENDU VULKAN ---
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {

            Renderer::Clear(); // Démarre la frame (Bleue)

            Renderer::BeginImGuiFrame();

            // Pour l'instant on garde notre Editeur en quarantaine, on teste juste la bête !
            ImGui::ShowDemoWindow();

            Renderer::EndImGuiFrame(); // Injecte l'interface

            Renderer::EndScene(); // Soumet au GPU

            // Support des Viewports ImGui sans crasher Wayland
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
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