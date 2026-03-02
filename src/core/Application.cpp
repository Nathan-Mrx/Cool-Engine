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

    // Dans le constructeur Application::Application
    m_TriangleEntity = m_Registry.create();
    m_Registry.emplace<TagComponent>(m_TriangleEntity, "Red Triangle");
    m_Registry.emplace<TransformComponent>(m_TriangleEntity);
    m_Registry.emplace<ColorComponent>(m_TriangleEntity, glm::vec3(0.8f, 0.2f, 0.3f));

    m_CameraEntity = m_Registry.create();
    m_Registry.emplace<TagComponent>(m_CameraEntity, "Main Camera");
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

        // --- FENÊTRE : INSPECTOR (DÉTAILS) ---
        ImGui::Begin("Inspector");

        if (m_SelectedContext != entt::null)
        {
            // --- 1. COMPOSANT : TAG (Toujours présent pour l'identification) ---
            if (m_Registry.all_of<TagComponent>(m_SelectedContext))
            {
                auto& tag = m_Registry.get<TagComponent>(m_SelectedContext).Tag;

                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, tag.c_str(), sizeof(buffer));

                if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
                    tag = std::string(buffer);
                }
            }

            ImGui::Separator();

            // --- 2. COMPOSANT : TRANSFORM ---
            if (m_Registry.all_of<TransformComponent>(m_SelectedContext))
            {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& tc = m_Registry.get<TransformComponent>(m_SelectedContext);
                    ImGui::DragFloat3("Position", &tc.Position[0], 0.1f);
                    ImGui::DragFloat3("Scale", &tc.Scale[0], 0.1f);
                    // On pourra ajouter la rotation ici plus tard !
                }
            }

            // --- 3. COMPOSANT : COLOR (Spécifique au rendu) ---
            if (m_Registry.all_of<ColorComponent>(m_SelectedContext))
            {
                if (ImGui::CollapsingHeader("Material / Color", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& cc = m_Registry.get<ColorComponent>(m_SelectedContext);
                    ImGui::ColorEdit3("Albedo", &cc.Color[0]);
                }
            }

            // --- 4. COMPOSANT : CAMERA (Spécifique aux caméras) ---
            if (m_Registry.all_of<CameraComponent>(m_SelectedContext))
            {
                if (ImGui::CollapsingHeader("Camera Settings", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& cam = m_Registry.get<CameraComponent>(m_SelectedContext);
                    ImGui::DragFloat3("Position", &cam.Position[0], 0.1f);
                    ImGui::DragFloat("Yaw", &cam.Yaw, 0.5f);
                    ImGui::DragFloat("Pitch", &cam.Pitch, 0.5f, -89.0f, 89.0f);

                    ImGui::Text("Front: %.2f, %.2f, %.2f", cam.Front.x, cam.Front.y, cam.Front.z);
                }
            }

            // --- BOUTON : ADD COMPONENT (Style Unreal/Godot) ---
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Add Component", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                ImGui::OpenPopup("AddComponent");
            }

            if (ImGui::BeginPopup("AddComponent")) {
                if (ImGui::MenuItem("Transform")) {
                    if (!m_Registry.all_of<TransformComponent>(m_SelectedContext))
                        m_Registry.emplace<TransformComponent>(m_SelectedContext);
                }
                if (ImGui::MenuItem("Color")) {
                    if (!m_Registry.all_of<ColorComponent>(m_SelectedContext))
                        m_Registry.emplace<ColorComponent>(m_SelectedContext, glm::vec3(1.0f));
                }
                ImGui::EndPopup();
            }

        }
        else
        {
            ImGui::Text("Select an entity to view its properties.");
        }

        ImGui::End();

        // --- FENÊTRE : SCENE HIERARCHY ---
        ImGui::Begin("Scene Hierarchy");

        m_Registry.view<TagComponent>().each([&](auto entity, auto& tag) {
            // On crée un label cliquable pour chaque entité
            ImGuiTreeNodeFlags flags = ((m_SelectedContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
            flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.Tag.c_str());

            // Si on clique sur le nom, on sélectionne l'entité
            if (ImGui::IsItemClicked()) {
                m_SelectedContext = entity;
            }

            if (opened) {
                ImGui::TreePop();
            }
        });

        // Clic droit dans le vide de la fenêtre pour créer une nouvelle entité (Style Godot)
        if (ImGui::BeginPopupContextWindow(0, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty Entity")) {
                auto e = m_Registry.create();
                m_Registry.emplace<TagComponent>(e, "Empty Entity");
            }
            ImGui::EndPopup();
        }

        ImGui::End();

        ImGui::End(); // Fin du DockSpaceParent

        // --- 4. LOGIQUE INPUTS (Placé après la fin du DockSpaceParent) ---
        if (m_ViewportHovered && Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            // On verrouille la souris pour la rotation "Unreal style"
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            auto& camera = m_Registry.get<CameraComponent>(m_CameraEntity);
            float cameraSpeed = 5.0f * m_DeltaTime;
            float sensitivity = 0.1f;

            // A. ROTATION (Souris)
            glm::vec2 mousePos = Input::GetMousePosition();
            glm::vec2 mouseDelta = mousePos - m_LastMousePosition;
            m_LastMousePosition = mousePos;

            camera.Yaw += mouseDelta.x * sensitivity;
            camera.Pitch -= mouseDelta.y * sensitivity;

            if (camera.Pitch > 89.0f) camera.Pitch = 89.0f;
            if (camera.Pitch < -89.0f) camera.Pitch = -89.0f;

            glm::vec3 dir;
            dir.x = cos(glm::radians(camera.Yaw)) * cos(glm::radians(camera.Pitch));
            dir.y = sin(glm::radians(camera.Yaw)) * cos(glm::radians(camera.Pitch));
            dir.z = sin(glm::radians(camera.Pitch));
            camera.Front = glm::normalize(dir);

            // B. DÉPLACEMENT (Clavier QWERTY)
            glm::vec3 cameraRight = glm::normalize(glm::cross(camera.WorldUp, camera.Front));

            if (Input::IsKeyPressed(GLFW_KEY_W)) camera.Position += camera.Front * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_S)) camera.Position -= camera.Front * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_A)) camera.Position -= cameraRight * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_D)) camera.Position += cameraRight * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_E)) camera.Position += camera.WorldUp * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_Q)) camera.Position -= camera.WorldUp * cameraSpeed;
        }
        else
        {
            // On libère la souris et on reset la position pour éviter le "jump" au prochain clic
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            m_LastMousePosition = Input::GetMousePosition();
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