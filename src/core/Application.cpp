#include "Application.h"
#include "Input.h"
#include "../ecs/Components.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <iostream>
#include <nfd.hpp>
#include <algorithm> // Pour std::transform

#include "project/Project.h"
#include "renderer/ModelLoader.h"
#include "scene/SceneSerializer.h"

// =========================================================================
// --- MAGIE DES TEMPLATES (RÉFLEXION UI) ---
// =========================================================================

template <typename T>
const char* GetComponentName() {
    if constexpr (std::is_same_v<T, TagComponent>) return "Tag";
    if constexpr (std::is_same_v<T, TransformComponent>) return "Transform";
    if constexpr (std::is_same_v<T, ColorComponent>) return "Color";
    if constexpr (std::is_same_v<T, CameraComponent>) return "Camera";
    if constexpr (std::is_same_v<T, MeshComponent>) return "Mesh";
    if constexpr (std::is_same_v<T, DirectionalLightComponent>) return "Directional Light";
    return "Unknown Component";
}

template<typename T>
void DrawComponentUI(entt::entity entity, entt::registry& registry) {
    if (registry.all_of<T>(entity)) {
        ImGui::PushID((void*)typeid(T).hash_code());
        auto& component = registry.get<T>(entity);
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;

        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
        float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImGui::Separator();

        bool open = ImGui::CollapsingHeader(GetComponentName<T>(), treeNodeFlags);
        ImGui::PopStyleVar();

        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
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
        ImGui::PopID();
    }
}

template<typename... Component>
void DrawAllComponents(entt::entity entity, entt::registry& registry) {
    ([&]() { DrawComponentUI<Component>(entity, registry); }(), ...);
}

template<typename... Component>
void DrawAddComponentMenu(entt::entity entity, entt::registry& registry) {
    if (ImGui::BeginPopup("AddComponent")) {
        ([&]() {
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
    glfwSwapInterval(1);

    NFD::Init();

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

    // Initialisation de la Grille Infinie (Quad 1x1 qui sera agrandi par le shader)
    float gridVertices[] = {
        -1.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,  1.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f, -1.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f
    };
    glGenVertexArrays(1, &m_GridVAO);
    glGenBuffers(1, &m_GridVBO);
    glBindVertexArray(m_GridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(gridVertices), gridVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    m_GridShader = std::make_unique<Shader>("shaders/grid.vert", "shaders/grid.frag");
    m_Shader = std::make_unique<Shader>("shaders/default.vert", "shaders/default.frag");

    // Création de la caméra par défaut
    m_CameraEntity = m_Registry.create();
    m_Registry.emplace<TagComponent>(m_CameraEntity, "Main Camera");
    m_Registry.emplace<CameraComponent>(m_CameraEntity);

    m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();
}

Application::~Application() {
    Shutdown();
    NFD::Quit();
}

void Application::DrawCreationMenu() {
    if (ImGui::MenuItem("Empty Entity")) m_SelectedContext = CreateEntity("Empty Entity");

    if (ImGui::BeginMenu("3D Objects")) {
        if (ImGui::MenuItem("Cube")) {
            auto e = CreateEntity("Cube");
            m_Registry.emplace<MeshComponent>(e).MeshData = ModelLoader::LoadModel("assets/primitives/cube.obj");
            m_Registry.emplace<ColorComponent>(e, glm::vec3(1.0f));
            m_SelectedContext = e;
        }
        if (ImGui::MenuItem("Sphere")) {
            auto e = CreateEntity("Sphere");
            m_Registry.emplace<MeshComponent>(e).MeshData = ModelLoader::LoadModel("assets/primitives/sphere.obj");
            m_Registry.emplace<ColorComponent>(e, glm::vec3(1.0f));
            m_SelectedContext = e;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Lights")) {
        if (ImGui::BeginMenu("Lights")) {
            if (ImGui::MenuItem("Directional Light")) {
                auto e = CreateEntity("Directional Light");
                m_Registry.emplace<DirectionalLightComponent>(e);

                // On l'incline par défaut pour voir tout de suite le relief
                auto& transform = m_Registry.get<TransformComponent>(e);
                transform.Rotation = { 45.0f, 45.0f, 0.0f };

                m_SelectedContext = e;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Camera")) {
        auto e = CreateEntity("Camera");
        m_Registry.emplace<CameraComponent>(e);
        m_SelectedContext = e;
    }
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

        auto& camera = m_Registry.get<CameraComponent>(m_CameraEntity);
        float aspectRatio = (float)m_Framebuffer->GetSpecification().Width / (float)m_Framebuffer->GetSpecification().Height;

        // Nouvelle projection pour l'échelle centimétrique (100 000 cm de vue)
        glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
        glm::mat4 view = glm::lookAtLH(camera.Position, camera.Position + camera.Front, camera.WorldUp);

        // --- RENDU DE LA GRILLE INFINIE ---
        if (m_ShowGrid && m_GridShader) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE); // Pour ne pas cacher les objets

            m_GridShader->Use();
            m_GridShader->SetMat4("uProjection", projection);
            m_GridShader->SetMat4("uView", view);
            m_GridShader->SetVec3("uCameraPos", camera.Position);

            glBindVertexArray(m_GridVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // --- RENDU DES MESHES ECS ---
        m_Shader->Use();
        m_Shader->SetMat4("uProjection", projection);
        m_Shader->SetMat4("uView", view);

        // --- GESTION DE LA LUMIÈRE DIRECTIONNELLE ---
        glm::vec3 lightDir = { 0.0f, 0.0f, -1.0f }; // Par défaut (Z vers le bas)
        glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
        float ambientStrength = 0.2f;
        float diffuseStrength = 0.8f;

        auto lightView = m_Registry.view<TransformComponent, DirectionalLightComponent>();
        for (auto entity : lightView) {
            auto& transform = lightView.get<TransformComponent>(entity);
            auto& light = lightView.get<DirectionalLightComponent>(entity);

            // On extrait l'axe Forward (X) directement depuis la matrice de transformation.
            // C'est très robuste et ça supporte toutes les rotations de l'éditeur !
            lightDir = glm::normalize(glm::vec3(transform.GetTransform()[0]));

            lightColor = light.Color;
            ambientStrength = light.AmbientIntensity;
            diffuseStrength = light.DiffuseIntensity;
            break; // On ne gère qu'une seule Directional Light globale pour l'instant
        }

        m_Shader->SetVec3("uLightDir", lightDir);
        m_Shader->SetVec3("uLightColor", lightColor);
        m_Shader->SetFloat("uAmbientStrength", ambientStrength);
        m_Shader->SetFloat("uDiffuseStrength", diffuseStrength);

        auto meshView = m_Registry.view<TransformComponent, MeshComponent, ColorComponent>();
        for (auto entity : meshView) {
            auto& transform = meshView.get<TransformComponent>(entity);
            auto& meshComp = meshView.get<MeshComponent>(entity);
            auto& color = meshView.get<ColorComponent>(entity);

            if (meshComp.MeshData) {
                m_Shader->SetVec3("uColor", color.Color);
                m_Shader->SetMat4("uModel", transform.GetTransform());
                meshComp.MeshData->Draw();
            }
        }
        m_Framebuffer->Unbind();

        // ==========================================
        // 2. RENDU DE L'ÉDITEUR (UI)
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
        ImGui::DockSpace(ImGui::GetID("MyEngineDockSpace"), ImVec2(0.0f, 0.0f), dockspace_flags);

        // --- MENU BAR ---
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                    m_Registry.clear();
                    Project::GetActive()->GetConfig().StartScene = ""; // Reset du chemin actuel
                }

                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    auto& startScene = Project::GetActive()->GetConfig().StartScene;
                    if (startScene.empty()) {
                        // Si pas de chemin, on redirige vers le Save As
                        // On peut appeler une fonction interne ou simuler le clic
                    } else {
                        SceneSerializer::Serialize(m_Registry, startScene);
                        std::cout << "[Cool Engine] Scene saved to: " << startScene << std::endl;
                    }
                }

                if (ImGui::MenuItem("Save Scene As...")) {
                    nfdchar_t* outPath = nullptr;
                    // Définition de notre extension personnalisée .cescene
                    nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };

                    // On propose le dossier "Scenes" du projet par défaut
                    std::string defaultPath = (Project::GetProjectDirectory() / "Scenes").string();

                    if (NFD::SaveDialog(outPath, filterItem, 1, defaultPath.c_str(), "Untitled.cescene") == NFD_OKAY) {
                        SceneSerializer::Serialize(m_Registry, outPath);
                        Project::GetActive()->GetConfig().StartScene = outPath; // On mémorise le chemin
                        NFD::FreePath(outPath);
                    }
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) m_Running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Grid", nullptr, &m_ShowGrid);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (Project::GetActive() == nullptr) {
            // ==========================================
            // ÉCRAN D'ACCUEIL : COOL ENGINE HUB
            // ==========================================
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_Once);

            ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
            ImGui::Begin("Cool Engine - Hub", nullptr, hubFlags);

            // --- HEADER STYLÉ ---
            ImGui::SetWindowFontScale(1.5f);
            ImGui::Text("COOL ENGINE");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextDisabled("Project Hub");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            // --- MISE EN PAGE : 2 COLONNES ---
            ImGui::Columns(2, "HubColumns", false);
            ImGui::SetColumnWidth(0, 450);

            // --- COLONNE GAUCHE : PROJETS RÉCENTS ---
            ImGui::Text("Recent Projects");
            ImGui::Dummy(ImVec2(0, 10));

            auto recents = Project::GetRecentProjects();

            if (recents.empty()) {
                ImGui::TextDisabled("No recent projects found...");
            } else {
                for (const auto& path : recents) {
                    bool exists = std::filesystem::exists(path);
                    if (!exists) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

                    std::string projectName = path.stem().string();

                    if (ImGui::Selectable(projectName.c_str()) && exists) {
                        Project::Load(path);
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", path.string().c_str());
                    }

                    if (!exists) ImGui::PopStyleColor();
                }
            }

            ImGui::NextColumn();

            // --- COLONNE DROITE : BOUTONS D'ACTION ---
            ImGui::Dummy(ImVec2(0, 50));

            if (ImGui::Button("Open Project...", ImVec2(-1, 50))) {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filterItem[1] = { { "Cool Engine Project", "ceproj" } };
                if (NFD::OpenDialog(outPath, filterItem, 1, nullptr) == NFD_OKAY) {
                    Project::Load(outPath);
                    NFD::FreePath(outPath);
                }
            }
            ImGui::Dummy(ImVec2(0, 10));
            if (ImGui::Button("New Project...", ImVec2(-1, 50))) {
                nfdchar_t* outPath = nullptr;
                if (NFD::PickFolder(outPath, nullptr) == NFD_OKAY) {
                    std::filesystem::path root = outPath;
                    NFD::FreePath(outPath);

                    // Création de l'arborescence style Unreal
                    std::filesystem::create_directories(root / "Content/Assets");
                    std::filesystem::create_directories(root / "Content/Scenes");
                    std::filesystem::create_directories(root / "Config");
                    std::filesystem::create_directories(root / "Binaries");

                    auto newProj = Project::New();
                    newProj->GetConfig().ProjectDirectory = root;
                    newProj->GetConfig().ContentDirectory = root / "Content";
                    newProj->GetConfig().Name = root.filename().string();

                    Project::SaveActive(root / (newProj->GetConfig().Name + ".ceproj"));
                }
            }
            ImGui::Columns(1);
            ImGui::End();
        }
        else {
            // --- ÉDITEUR PRINCIPAL ---
            m_ContentBrowserPanel->OnImGuiRender();

            // VIEWPORT
            ImGui::Begin("Viewport");
            ImVec2 viewportSize = ImGui::GetContentRegionAvail();
            uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
            ImGui::Image((ImTextureID)(uintptr_t)textureID, viewportSize, ImVec2{0, 1}, ImVec2{1, 0});
            m_ViewportHovered = ImGui::IsItemHovered();

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* relPath = (const char*)payload->Data;
                    std::filesystem::path fullPath = Project::GetContentDirectory() / relPath;

                    std::string ext = fullPath.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") {
                        auto entity = CreateEntity(fullPath.stem().string());
                        auto& mc = m_Registry.emplace<MeshComponent>(entity);
                        m_Registry.emplace<ColorComponent>(entity, glm::vec3(1.0f));
                        mc.MeshData = ModelLoader::LoadModel(fullPath.string());
                        m_SelectedContext = entity;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::End();

            // HIERARCHY
            ImGui::Begin("Scene Hierarchy");
            if (ImGui::Button("+", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                ImGui::OpenPopup("HierarchyCreateMenu");
            }
            if (ImGui::BeginPopup("HierarchyCreateMenu")) {
                DrawCreationMenu();
                ImGui::EndPopup();
            }
            ImGui::Separator();

            auto view = m_Registry.view<TagComponent>();
            for (auto entity : view) {
                auto& tag = view.get<TagComponent>(entity);
                ImGuiTreeNodeFlags flags = ((m_SelectedContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

                // On cast l'entité en void* de manière sécurisée (sur Arch Linux / 64-bit)
                bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entity, flags, "%s", tag.Tag.c_str());

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
            }

            if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                DrawCreationMenu();
                ImGui::EndPopup();
            }
            ImGui::End();

            // INSPECTOR
            ImGui::Begin("Inspector");
            if (m_SelectedContext != entt::null) {
                std::apply([&](auto... args) {
                    DrawAllComponents<decltype(args)...>(m_SelectedContext, m_Registry);
                }, AllComponents{});

                ImGui::Dummy(ImVec2(0.0f, 20.0f));
                if (ImGui::Button("Add Component", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    ImGui::OpenPopup("AddComponent");
                }

                std::apply([&](auto... args) {
                    DrawAddComponentMenu<decltype(args)...>(m_SelectedContext, m_Registry);
                }, AllComponents{});

                ImGui::Dummy(ImGui::GetContentRegionAvail());
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* relPath = (const char*)payload->Data;
                        std::filesystem::path fullPath = Project::GetContentDirectory() / relPath;

                        std::string ext = fullPath.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") {
                            auto& mc = m_Registry.get_or_emplace<MeshComponent>(m_SelectedContext);
                            mc.MeshData = ModelLoader::LoadModel(fullPath.string());
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            } else {
                ImGui::Text("Select an entity to view its properties.");
            }
            ImGui::End();
        }
        ImGui::End();

        // ==========================================
        // 3. LOGIQUE INPUTS (Caméra à 500 cm/s)
        // ==========================================

        bool control = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
        if (control && Input::IsKeyPressed(GLFW_KEY_S)) {
            auto& currentScene = Project::GetActive()->GetConfig().StartScene;
            if (!currentScene.empty()) {
                SceneSerializer::Serialize(m_Registry, currentScene);
                std::cout << "[Cool Engine] Quick Save successful!" << std::endl;
            } else {
                // Optionnel : Déclencher le dialogue "Save As" ici si c'est une nouvelle scène
                std::cout << "[Cool Engine] No active scene path. Use 'Save As' first." << std::endl;
            }
        }

        if (m_ViewportHovered && Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            float cameraSpeed = 500.0f * m_DeltaTime;
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
    glDeleteVertexArrays(1, &m_GridVAO);
    glDeleteBuffers(1, &m_GridVBO);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

entt::entity Application::CreateEntity(const std::string& name) {
    auto entity = m_Registry.create();
    m_Registry.emplace<TagComponent>(entity, name.empty() ? "Entity" : name);
    m_Registry.emplace<TransformComponent>(entity);
    return entity;
}