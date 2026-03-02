#include "Application.h"
#include "Input.h"
#include "../ecs/Components.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h> // Requis pour GImGui
#include <iostream>

// =========================================================================
// --- MAGIE DES TEMPLATES (RÉFLEXION UI) ---
// =========================================================================

// Fonction utilitaire pour obtenir le nom propre d'un composant
template <typename T>
const char* GetComponentName() {
    if constexpr (std::is_same_v<T, TagComponent>) return "Tag";
    if constexpr (std::is_same_v<T, TransformComponent>) return "Transform";
    if constexpr (std::is_same_v<T, ColorComponent>) return "Color";
    if constexpr (std::is_same_v<T, CameraComponent>) return "Camera";
    return "Unknown Component";
}

// Dessine un composant spécifique s'il existe sur l'entité
template<typename T>
void DrawComponentUI(entt::entity entity, entt::registry& registry) {
    if (registry.all_of<T>(entity)) {
        // --- 1. ON CRÉE UN CONTEXTE UNIQUE POUR CE COMPOSANT ---
        ImGui::PushID((void*)typeid(T).hash_code());

        auto& component = registry.get<T>(entity);
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;

        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
        float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImGui::Separator();

        // Plus besoin de la bidouille du "##", PushID s'en occupe en coulisses !
        bool open = ImGui::CollapsingHeader(GetComponentName<T>(), treeNodeFlags);
        ImGui::PopStyleVar();

        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
        // Le bouton "+" n'entrera plus en conflit avec les autres
        if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight })) {
            ImGui::OpenPopup("ComponentSettings");
        }

        bool removeComponent = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            if (ImGui::MenuItem("Remove component")) removeComponent = true;
            ImGui::EndPopup();
        }

        if (open) {
            component.OnImGuiRender();
        }

        if (removeComponent) registry.remove<T>(entity);

        // --- 2. ON FERME LE CONTEXTE ---
        ImGui::PopID();
    }
}

// Fold expression pour dessiner tous les composants présents sur l'entité
template<typename... Component>
void DrawAllComponents(entt::entity entity, entt::registry& registry) {
    ([&]() { DrawComponentUI<Component>(entity, registry); }(), ...);
}

// Fold expression pour remplir dynamiquement le menu "Add Component"
template<typename... Component>
void DrawAddComponentMenu(entt::entity entity, entt::registry& registry) {
    if (ImGui::BeginPopup("AddComponent")) {
        ([&]() {
            // N'affiche le composant dans la liste que si l'entité ne l'a pas déjà
            if (!registry.all_of<Component>(entity)) {
                if (ImGui::MenuItem(GetComponentName<Component>())) {
                    registry.emplace<Component>(entity);
                    ImGui::CloseCurrentPopup();
                }
            }
        }(), ...);
        ImGui::EndPopup();
    }
}

// =========================================================================
// --- CLASSE APPLICATION ---
// =========================================================================

Application::Application(const std::string& name, int width, int height) {
    if (!glfwInit()) {
        std::cerr << "Could not initialize GLFW!" << std::endl;
        return;
    }

    m_Window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
    glfwMakeContextCurrent(m_Window);
    Input::Init(m_Window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1); // VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 460");

    FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    m_Framebuffer = std::make_unique<Framebuffer>(fbSpec);

    float vertices[] = {
        0.0f, -0.5f, -0.5f,
        0.0f,  0.5f, -0.5f,
        0.0f,  0.0f,  0.5f
    };

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_Shader = std::make_unique<Shader>("shaders/default.vert", "shaders/default.frag");

    // Création des entités initiales
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
        float currentFrameTime = static_cast<float>(glfwGetTime());
        m_DeltaTime = currentFrameTime - m_LastFrameTime;
        m_LastFrameTime = currentFrameTime;

        glfwPollEvents();

        // ==========================================
        // 1. RENDU DU MOTEUR
        // ==========================================
        m_Framebuffer->Bind();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_Shader->Use();
        glBindVertexArray(m_VAO);

        float aspectRatio = (float)m_Framebuffer->GetSpecification().Width / (float)m_Framebuffer->GetSpecification().Height;
        glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
        m_Shader->SetMat4("uProjection", projection);

        auto& camera = m_Registry.get<CameraComponent>(m_CameraEntity);
        glm::mat4 view = glm::lookAtLH(camera.Position, camera.Position + camera.Front, camera.WorldUp);
        m_Shader->SetMat4("uView", view);

        auto renderView = m_Registry.view<TransformComponent, ColorComponent>();
        for (auto entity : renderView) {
            auto& transform = renderView.get<TransformComponent>(entity);
            auto& color = renderView.get<ColorComponent>(entity);

            m_Shader->SetVec3("uColor", color.Color);

            // On utilise la nouvelle méthode propre !
            m_Shader->SetMat4("uModel", transform.GetTransform());

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindVertexArray(0);
        m_Framebuffer->Unbind();

        // ==========================================
        // 2. RENDU DE L'ÉDITEUR
        // ==========================================
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        BeginImGui();

        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(main_viewport->WorkPos);
        ImGui::SetNextWindowSize(main_viewport->WorkSize);
        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("DockSpaceParent", nullptr, window_flags);
        ImGui::PopStyleVar(2);

        ImGuiID dockspace_id = ImGui::GetID("MyEngineDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

        // --- MENU BAR ---
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) m_Running = false;
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // --- VIEWPORT ---
        ImGui::Begin("Viewport");
        m_ViewportHovered = ImGui::IsWindowHovered();
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (m_Framebuffer->GetSpecification().Width != viewportSize.x || m_Framebuffer->GetSpecification().Height != viewportSize.y) {
            m_Framebuffer->Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }
        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image((ImTextureID)(uint64_t)textureID, viewportSize, ImVec2{0, 1}, ImVec2{1, 0});
        ImGui::End();

        // --- SCENE HIERARCHY ---
        ImGui::Begin("Scene Hierarchy");
        m_Registry.view<TagComponent>().each([&](auto entity, auto& tag) {
            ImGuiTreeNodeFlags flags = ((m_SelectedContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.Tag.c_str());

            if (ImGui::IsItemClicked()) m_SelectedContext = entity;

            bool entityDeleted = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete Entity")) entityDeleted = true;
                ImGui::EndPopup();
            }

            if (opened) ImGui::TreePop();

            if (entityDeleted) {
                m_Registry.destroy(entity);
                if (m_SelectedContext == entity) m_SelectedContext = entt::null;
            }
        });

        if (ImGui::BeginPopupContextWindow(0, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty Entity")) {
                auto e = m_Registry.create();
                m_Registry.emplace<TagComponent>(e, "Empty Entity");
                m_SelectedContext = e; // Sélectionne la nouvelle entité automatiquement
            }
            ImGui::EndPopup();
        }
        ImGui::End();

        // --- INSPECTOR AUTOMATIQUE ---
        ImGui::Begin("Inspector");
        if (m_SelectedContext != entt::null) {

            // 1. On dessine tous les composants existants
            std::apply([&](auto... args) {
                DrawAllComponents<decltype(args)...>(m_SelectedContext, m_Registry);
            }, AllComponents{});

            // 2. Bouton d'ajout automatique
            ImGui::Dummy(ImVec2(0.0f, 20.0f));
            if (ImGui::Button("Add Component", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                ImGui::OpenPopup("AddComponent");
            }

            std::apply([&](auto... args) {
                DrawAddComponentMenu<decltype(args)...>(m_SelectedContext, m_Registry);
            }, AllComponents{});

        } else {
            ImGui::Text("Select an entity to view its properties.");
        }
        ImGui::End();

        ImGui::End(); // Fin du DockSpaceParent

        // ==========================================
        // 3. LOGIQUE INPUTS (Caméra)
        // ==========================================
        if (m_ViewportHovered && Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            float cameraSpeed = 5.0f * m_DeltaTime;
            float sensitivity = 0.1f;

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

            glm::vec3 cameraRight = glm::normalize(glm::cross(camera.WorldUp, camera.Front));

            if (Input::IsKeyPressed(GLFW_KEY_W)) camera.Position += camera.Front * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_S)) camera.Position -= camera.Front * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_A)) camera.Position -= cameraRight * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_D)) camera.Position += cameraRight * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_E)) camera.Position += camera.WorldUp * cameraSpeed;
            if (Input::IsKeyPressed(GLFW_KEY_Q)) camera.Position -= camera.WorldUp * cameraSpeed;
        } else {
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