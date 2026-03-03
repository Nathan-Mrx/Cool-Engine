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


Application* Application::s_Instance = nullptr;

Application::Application(const std::string& name, int width, int height) {
    s_Instance = this;

    // 1. Initialisation système (GLFW, GLAD, etc.)
    if (!glfwInit()) return;
    m_Window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
    glfwMakeContextCurrent(m_Window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);

    Input::Init(m_Window);
    NFD::Init();

    // 2. INITIALISATION IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // Pour ton layout macOS-style
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Permet de sortir les fenêtres de l'app

    // Setup du style (Sombre par défaut, très pro)
    ImGui::StyleColorsDark();

    // Liaison avec les backends
    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 460"); // Aligné sur tes shaders

    // 3. Initialisation des moteurs de haut niveau
    Renderer::Init();
    m_EditorLayer = std::make_unique<EditorLayer>();
    m_EditorLayer->OnAttach();
}

Application::~Application() {
    m_EditorLayer->OnDetach();

    // SHUTDOWN IMGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
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

        glfwPollEvents();

        // 1. Début de la frame ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 2. Logique & Rendu de l'Editeur
        m_EditorLayer->OnUpdate(m_DeltaTime);
        m_EditorLayer->OnImGuiRender();

        // 3. Fin de la frame ImGui
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // ---> AJOUTE CE BLOC ICI POUR LES VIEWPORTS <---
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(m_Window);
    }
}

void Application::SetWindowIcon(const std::string& path) {
    GLFWimage images[1];
    int channels;
    // On charge les pixels de l'icône (PNG recommandé)
    images[0].pixels = stbi_load(path.c_str(), &images[0].width, &images[0].height, &channels, 4);

    if (images[0].pixels) {
        glfwSetWindowIcon(m_Window, 1, images);
        stbi_image_free(images[0].pixels);
    }
}