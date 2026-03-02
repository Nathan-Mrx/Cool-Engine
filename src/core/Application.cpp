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
        // --- CALCUL DU DELTA TIME ---
        float currentFrameTime = static_cast<float>(glfwGetTime());
        m_DeltaTime = currentFrameTime - m_LastFrameTime;
        m_LastFrameTime = currentFrameTime;

        glfwPollEvents();


        // --- LOGIQUE DE JEU / CAMÉRA ---
        // On n'active les contrôles QUE si on clique droit à l'intérieur du Viewport
        auto& camera = m_Registry.get<CameraComponent>(m_CameraEntity);

        if (m_ViewportHovered && Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            // --- GESTION DU CURSEUR (Optionnel mais recommandé) ---
            // Cache le curseur pour pouvoir tourner à l'infini sans taper les bords de l'écran
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            // --- CALCUL DU DELTA SOURIS ---
            glm::vec2 mousePos = Input::GetMousePosition();
            glm::vec2 mouseDelta = mousePos - m_LastMousePosition;
            m_LastMousePosition = mousePos;

            auto& camera = m_Registry.get<CameraComponent>(m_CameraEntity);

            float sensitivity = 0.1f;
            camera.Yaw += mouseDelta.x * sensitivity;
            camera.Pitch -= mouseDelta.y * sensitivity; // Inversé car Y va vers le bas en coordonnées écran

            // On contraint le Pitch pour ne pas pouvoir faire de salto arrière (limite à 89°)
            if (camera.Pitch > 89.0f) camera.Pitch = 89.0f;
            if (camera.Pitch < -89.0f) camera.Pitch = -89.0f;

            // --- MATHÉMATIQUES : Conversion Angles -> Vecteur Direction ---
            glm::vec3 direction;
            direction.x = cos(glm::radians(camera.Yaw)) * cos(glm::radians(camera.Pitch));
            direction.y = sin(glm::radians(camera.Yaw)) * cos(glm::radians(camera.Pitch));
            direction.z = sin(glm::radians(camera.Pitch));
            camera.Front = glm::normalize(direction);
            float cameraSpeed = 5.0f * m_DeltaTime;

            glm::vec3 cameraRight = glm::normalize(glm::cross(camera.WorldUp, camera.Front));

            // ZQSD / WASD
            if (Input::IsKeyPressed(GLFW_KEY_W)) camera.Position += camera.Front * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_S)) camera.Position -= camera.Front * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_A)) camera.Position -= cameraRight * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_D)) camera.Position += cameraRight * cameraSpeed;

            // Monter / Descendre (E / Q)
            if (Input::IsKeyPressed(GLFW_KEY_E)) camera.Position += camera.WorldUp * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_Q)) camera.Position -= camera.WorldUp * cameraSpeed;
        }
        else
        {
            // Réaffiche le curseur quand on relâche le clic droit
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            // On met à jour la position de référence pour éviter un saut au prochain clic
            m_LastMousePosition = Input::GetMousePosition();
        }

        // ==========================================
        // ETAPE 1 : RENDU DU MOTEUR (Dans le Viewport)
        // ==========================================
        m_Framebuffer->Bind();
        glEnable(GL_DEPTH_TEST); // Active la profondeur 3D

        // Fond du viewport
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // On nettoie aussi le Z-Buffer !

        /// --- SYSTÈME DE RENDU ---
        m_Shader->Use();
        glBindVertexArray(m_VAO);

        // 1. Matrice de Projection (Dynamique selon la taille du Framebuffer)
        float aspectRatio = (float)m_Framebuffer->GetSpecification().Width / (float)m_Framebuffer->GetSpecification().Height;
        glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
        m_Shader->SetMat4("uProjection", projection);

        // 2. Matrice de Vue
        glm::mat4 view = glm::lookAtLH(camera.Position, camera.Position + camera.Front, camera.WorldUp);
        m_Shader->SetMat4("uView", view);

        // 3. Boucle sur les entités
        auto renderView = m_Registry.view<TransformComponent, ColorComponent>();
        for (auto entity : renderView) {
            auto& transform = renderView.get<TransformComponent>(entity);
            auto& color = renderView.get<ColorComponent>(entity);

            m_Shader->SetVec3("uColor", color.Color);

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, transform.Position);
            model = glm::scale(model, transform.Scale);
            m_Shader->SetMat4("uModel", model);

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindVertexArray(0);

        m_Framebuffer->Unbind();

        // ==========================================
        // ETAPE 2 : RENDU DE L'ÉDITEUR (Interface)
        // ==========================================
        glDisable(GL_DEPTH_TEST); // Désactivé pour l'UI

        // Couleur de fond de la fenêtre principale (derrière ImGui)
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        BeginImGui();

        // --- FENÊTRE DES PARAMÈTRES ---
        ImGui::Begin("Engine Settings");

        ImGui::Text("Performances");
        ImGui::Text("Delta Time: %.4f ms", m_DeltaTime * 1000.0f);
        ImGui::Text("FPS: %.1f", 1.0f / m_DeltaTime);
        ImGui::Separator();

        auto& triangleColor = m_Registry.get<ColorComponent>(m_TriangleEntity);
        auto& triangleTransform = m_Registry.get<TransformComponent>(m_TriangleEntity);

        ImGui::ColorEdit3("Entity Color", &triangleColor.Color[0]);
        ImGui::DragFloat3("Position", &triangleTransform.Position[0], 0.1f);
        ImGui::DragFloat3("Scale", &triangleTransform.Scale[0], 0.1f);

        ImGui::Separator();
        ImGui::Text("Camera Controls");
        auto& cameraComp = m_Registry.get<CameraComponent>(m_CameraEntity);
        ImGui::DragFloat3("Cam Position", &cameraComp.Position[0], 0.1f);

        ImGui::End();

        // --- FENÊTRE DU VIEWPORT ---
        ImGui::Begin("Viewport");

        // On vérifie si la souris survole CETTE fenêtre spécifiquement
        m_ViewportHovered = ImGui::IsWindowHovered();

        // Gestion du redimensionnement
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_Framebuffer->GetSpecification().Width != viewportPanelSize.x ||
            m_Framebuffer->GetSpecification().Height != viewportPanelSize.y) {

            m_Framebuffer->Resize((uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y);
        }

        // Affichage de la texture 3D
        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image((ImTextureID)(uint64_t)textureID, ImVec2{viewportPanelSize.x, viewportPanelSize.y}, ImVec2{0, 1}, ImVec2{1, 0});

        ImGui::End();

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