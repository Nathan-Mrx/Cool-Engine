#include "Application.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include "Input.h"

Application::Application(const std::string& name, int width, int height) {
    if (!glfwInit()) {
        std::cerr << "Could not initialize GLFW!" << std::endl;
        return;
    }

    m_Window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
    glfwMakeContextCurrent(m_Window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1); // VSync ON

    // On connecte notre système d'input à la fenêtre GLFW
    Input::Init(m_Window);

    // Initialisation ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 460");

    // --- INITIALISATION DU FRAMEBUFFER ---
    FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    m_Framebuffer = std::make_unique<Framebuffer>(fbSpec);

    // 1. Définition des sommets du triangle (x, y, z)
    float vertices[] = {
        0.0f, -0.5f, -0.5f, // Bas gauche
        0.0f,  0.5f, -0.5f, // Bas droite
        0.0f,  0.0f,  0.5f  // Haut centre (la pointe pointe vers le ciel Z)
    };

    // 2. Génération du VAO et VBO
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    // 3. On "bind" (active) le VAO en premier
    glBindVertexArray(m_VAO);

    // 4. On bind le VBO et on y envoie nos données
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 5. On explique à OpenGL comment lire ces données (layout = 0, 3 floats)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 6. On dé-bind
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // 7. On charge notre Shader compilé !
    m_Shader = std::make_unique<Shader>("shaders/default.vert", "shaders/default.frag");

    // --- INITIALISATION ECS ---
    m_TriangleEntity = m_Registry.create();
    m_CameraEntity = m_Registry.create();

    // On attache les composants à nos entités
    m_Registry.emplace<TransformComponent>(m_TriangleEntity);
    m_Registry.emplace<ColorComponent>(m_TriangleEntity, glm::vec3(0.8f, 0.2f, 0.3f));
    m_Registry.emplace<CameraComponent>(m_CameraEntity);
}

Application::~Application() {
    Shutdown();
}

void Application::Run() {
    while (m_Running && !glfwWindowShouldClose(m_Window)) {
        // --- 1. GESTION DU TEMPS ---
        float currentFrameTime = static_cast<float>(glfwGetTime());
        m_DeltaTime = currentFrameTime - m_LastFrameTime;
        m_LastFrameTime = currentFrameTime;

        glfwPollEvents();

        // --- 2. RENDU DU MOTEUR (Dans le Framebuffer) ---
        m_Framebuffer->Bind();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_Shader->Use();
        glBindVertexArray(m_VAO);

        // Projection dynamique selon le ratio du viewport
        float aspectRatio = (float)m_Framebuffer->GetSpecification().Width / (float)m_Framebuffer->GetSpecification().Height;
        glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
        m_Shader->SetMat4("uProjection", projection);

        // Vue & Rendu ECS
        auto& camera = m_Registry.get<CameraComponent>(m_CameraEntity);
        glm::mat4 view = glm::lookAtLH(camera.Position, camera.Position + camera.Front, camera.WorldUp);
        m_Shader->SetMat4("uView", view);

        auto renderView = m_Registry.view<TransformComponent, ColorComponent>();
        for (auto entity : renderView) {
            auto& transform = renderView.get<TransformComponent>(entity);
            auto& color = renderView.get<ColorComponent>(entity);

            m_Shader->SetVec3("uColor", color.Color);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.Position) * glm::scale(glm::mat4(1.0f), transform.Scale);
            m_Shader->SetMat4("uModel", model);

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindVertexArray(0);
        m_Framebuffer->Unbind();

        // --- 3. RENDU DE L'ÉDITEUR (Interface ImGui) ---
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f); // Fond style macOS Dark
        glClear(GL_COLOR_BUFFER_BIT);

        BeginImGui();

        // --- DOCKSPACE ROOT ---
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(main_viewport->WorkPos);
        ImGui::SetNextWindowSize(main_viewport->WorkSize);
        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("DockSpaceParent", nullptr, window_flags);
        ImGui::PopStyleVar(2);

        // L'ID du DockSpace pour que les fenêtres puissent s'y ancrer
        ImGuiID dockspace_id = ImGui::GetID("MyEngineDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) m_Running = false;
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // --- FENÊTRE : VIEWPORT ---
        ImGui::Begin("Viewport");
        m_ViewportHovered = ImGui::IsWindowHovered();

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (m_Framebuffer->GetSpecification().Width != viewportSize.x || m_Framebuffer->GetSpecification().Height != viewportSize.y) {
            m_Framebuffer->Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image((ImTextureID)(uint64_t)textureID, viewportSize, ImVec2{0, 1}, ImVec2{1, 0});
        ImGui::End();

        // --- FENÊTRE : INSPECTOR / SETTINGS ---
        ImGui::Begin("Inspector");
        ImGui::Text("FPS: %.1f", 1.0f / m_DeltaTime);
        ImGui::Separator();

        auto& triangleColor = m_Registry.get<ColorComponent>(m_TriangleEntity);
        ImGui::ColorEdit3("Entity Color", &triangleColor.Color[0]);

        auto& camComp = m_Registry.get<CameraComponent>(m_CameraEntity);
        ImGui::DragFloat3("Cam Position", &camComp.Position[0], 0.1f);
        ImGui::End();

        ImGui::End(); // Fin du DockSpaceParent

        // --- 4. LOGIQUE INPUTS (Seulement si focus viewport) ---
        if (m_ViewportHovered && Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            // ... (Ici ta logique de mouvement Pitch/Yaw et ZQSD que tu as déjà) ...
        } else {
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        EndImGui();
        glfwSwapBuffers(m_Window);
    }
}

void Application::BeginImGui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Application::EndImGui() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::Shutdown() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}