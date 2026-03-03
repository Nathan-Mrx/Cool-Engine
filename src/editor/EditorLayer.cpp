#include "EditorLayer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glad/glad.h>

#include "core/Application.h"
#include "core/Input.h"
#include "ecs/Components.h"
#include "project/Project.h"
#include "renderer/Renderer.h"
#include "scene/SceneSerializer.h"
#include <stb_image_write.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

void EditorLayer::OnAttach() {
    m_ActiveScene = std::make_shared<Scene>();
    // Utilisation du nom correct défini dans le header
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();

    m_ContentBrowserPanel->OnSceneOpenCallback = [this](const std::filesystem::path& path) {
        // 1. On prépare une nouvelle scène
        m_ActiveScene = std::make_shared<Scene>();
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        // 2. On charge les données via le Serializer
        SceneSerializer serializer(m_ActiveScene);
        serializer.Deserialize(path.string());

        std::cout << "[Editor] Scene loaded: " << path.filename() << std::endl;
    };

    FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    m_ViewportFramebuffer = std::make_unique<Framebuffer>(fbSpec);

    // Si un projet a été chargé via la ligne de commande (CLI)
    if (Project::GetActive())
    {
        // On récupère la scène de démarrage du projet
        // (Ou on crée une scène par défaut si le projet est neuf)
        m_ActiveScene = std::make_shared<Scene>();
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        // Optionnel : charger le fichier .cescene par défaut du projet ici
    }
}

void EditorLayer::OnDetach() {
    // Nettoyage si nécessaire
}

void EditorLayer::DrawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {

            // --- NOUVEAU : CRÉER UNE SCÈNE VIERGE ---
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                // 1. On écrase l'ancien pointeur par une nouvelle scène vierge
                m_ActiveScene = std::make_shared<Scene>();

                // 2. On met à jour l'interface (ce qui désélectionne aussi l'entité active)
                m_SceneHierarchyPanel.SetContext(m_ActiveScene);
            }

            ImGui::Separator();

            // --- NOUVEAU : SAUVEGARDE RAPIDE ---
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                SaveScene();
            }

            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                nfdchar_t* outPath = nullptr;
                // Le filtre "cescene" garantit la bonne extension
                nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };
                if (NFD::SaveDialog(outPath, filterItem, 1, nullptr, "Untitled.cescene") == NFD_OKAY) {
                    m_CurrentScenePath = outPath; // Mise à jour du chemin actuel
                    SceneSerializer serializer(m_ActiveScene);
                    serializer.Serialize(outPath);
                    NFD::FreePath(outPath);
                }
            }

            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };
                if (NFD::OpenDialog(outPath, filterItem, 1, nullptr) == NFD_OKAY) {
                    m_CurrentScenePath = outPath; // Mise à jour du chemin actuel

                    // 1. On crée une nouvelle scène vierge
                    m_ActiveScene = std::make_shared<Scene>();
                    // 2. On met à jour le panneau de hiérarchie pour qu'il pointe sur la nouvelle
                    m_SceneHierarchyPanel.SetContext(m_ActiveScene);

                    // 3. On charge les données
                    SceneSerializer serializer(m_ActiveScene);
                    serializer.Deserialize(outPath);

                    NFD::FreePath(outPath);
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) {
                m_RequestCloseProject = true;
            }
            if (ImGui::MenuItem("Exit")) Application::Get().Close();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Project Settings")) {
                m_ShowProjectSettings = true;
            }
            ImGui::EndMenu();
        }

        // --- NOUVEAU : Menu View pour la grille ---
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Grid", nullptr, &m_ShowGrid);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void EditorLayer::BeginDockspace() {
    static bool dockspaceOpen = true;
    ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("DockSpaceParent", &dockspaceOpen, window_flags);
    ImGui::PopStyleVar(2);

    ImGuiID dockspace_id = ImGui::GetID("MyEngineDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    // --- LOGIQUE DE LAYOUT PAR DÉFAUT ---
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr || ImGui::DockBuilderGetNode(dockspace_id)->ChildNodes[0] == 0) {

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_id_main = dockspace_id;

        ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Left, 0.20f, nullptr, &dock_id_main);
        ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Right, 0.25f, nullptr, &dock_id_main);
        ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Down, 0.30f, nullptr, &dock_id_main);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_id_left);
        ImGui::DockBuilderDockWindow("Inspector", dock_id_right);
        ImGui::DockBuilderDockWindow("Content Browser", dock_id_bottom);
        ImGui::DockBuilderDockWindow("Viewport", dock_id_main);

        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock_id_main);
        if (node) {
            node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }

        ImGui::DockBuilderFinish(dockspace_id);
    }
}

void EditorLayer::EndDockspace() {
    ImGui::End();
}

void EditorLayer::CloseProjectInternal() {
    Project::Unload();

    m_ActiveScene = std::make_shared<Scene>();
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
}

void EditorLayer::OnUpdate(float ts) {
    // --- 1. DÉTECTION DE CHARGEMENT DE PROJET AUTOMATIQUE ---
    static std::shared_ptr<Project> s_LastProject = nullptr;
    auto currentProject = Project::GetActive();

    if (currentProject && currentProject != s_LastProject) {
        s_LastProject = currentProject;

        m_ActiveScene = std::make_shared<Scene>();
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        std::filesystem::path scenePath = currentProject->GetProjectDirectory() / currentProject->GetConfig().StartScene;

        if (std::filesystem::exists(scenePath)) {
            SceneSerializer serializer(m_ActiveScene);
            serializer.Deserialize(scenePath.string());
            m_CurrentScenePath = scenePath; // On mémorise la scène de départ
            std::cout << "[Editor] Loaded default scene: " << currentProject->GetConfig().StartScene << std::endl;
        } else {
            std::cout << "[Editor] Warning: Default scene not found at " << scenePath << std::endl;
        }
    }

    if (!currentProject) {
        s_LastProject = nullptr;
        return;
    }

    // --- 2. VÉRIFICATIONS DE SÉCURITÉ DU VIEWPORT ---
    if (!m_ActiveScene || !m_ViewportFramebuffer) return;

    if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f) return;
    m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

    // --- 3. GESTION DE LA CAMÉRA ---
    GLFWwindow* window = Application::Get().GetWindow();
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (m_ViewportFocused) {
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }
    }

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        glm::vec2 mousePos = { (float)mouseX, (float)mouseY };
        glm::vec2 delta = (mousePos - m_LastMousePosition) * 0.003f;
        m_LastMousePosition = mousePos;

        m_EditorCamera.Yaw += delta.x;
        m_EditorCamera.Pitch -= delta.y;
        m_EditorCamera.Pitch = glm::clamp(m_EditorCamera.Pitch, glm::radians(-89.0f), glm::radians(89.0f));

        glm::vec3 front;
        front.x = cos(m_EditorCamera.Yaw) * cos(m_EditorCamera.Pitch);
        front.y = sin(m_EditorCamera.Yaw) * cos(m_EditorCamera.Pitch);
        front.z = sin(m_EditorCamera.Pitch);
        m_EditorCamera.Front = glm::normalize(front);

        float baseSpeed = 500.0f;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            baseSpeed *= 4.0f;
        }

        float cameraSpeed = baseSpeed * ts;
        glm::vec3 right = glm::normalize(glm::cross(m_EditorCamera.WorldUp, m_EditorCamera.Front));

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_EditorCamera.Position += m_EditorCamera.Front * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) m_EditorCamera.Position -= m_EditorCamera.Front * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) m_EditorCamera.Position -= right * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) m_EditorCamera.Position += right * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) m_EditorCamera.Position += m_EditorCamera.WorldUp * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) m_EditorCamera.Position -= m_EditorCamera.WorldUp * cameraSpeed;

    } else {
        m_LastMousePosition = { (float)mouseX, (float)mouseY };
    }

    // --- 4. RENDU DE LA SCÈNE ---
    m_ViewportFramebuffer->Bind();
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    Renderer::Clear();

    float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
    glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
    glm::mat4 view = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);

    Renderer::BeginScene(view, projection, m_EditorCamera.Position);
    Renderer::DrawGrid(m_ShowGrid);
    Renderer::RenderScene(m_ActiveScene.get());

    auto lightView = m_ActiveScene->m_Registry.view<TransformComponent, DirectionalLightComponent>();
    for (auto entity : lightView) {
        auto [transform, light] = lightView.get<TransformComponent, DirectionalLightComponent>(entity);

        glm::vec3 lightDirection = glm::normalize(transform.Rotation * glm::vec3(0.0f, -1.0f, 0.0f));
        glm::vec3 rightAxis = transform.GetRightVector();
        glm::vec3 upAxis    = transform.GetForwardVector();

        Renderer::DrawDebugArrow(
            transform.Location,
            lightDirection,
            rightAxis,
            upAxis,
            glm::vec3(1.0f, 0.9f, 0.1f),
            view, projection,
            50.0f
        );
    }

    Renderer::EndScene();

    // --- 5. LOGIQUE DE FERMETURE ET CAPTURE THUMBNAIL ---
    bool isAppClosing = glfwWindowShouldClose(Application::Get().GetWindow());

    if (m_RequestCloseProject || isAppClosing) {
        if (currentProject) {
            CaptureViewportThumbnail(currentProject->GetProjectDirectory().string());

            if (!isAppClosing) {
                CloseProjectInternal();
                m_RequestCloseProject = false;
            }
        }
    }

    m_ViewportFramebuffer->Unbind();
}

void EditorLayer::OnImGuiRender() {
    BeginDockspace();
    DrawMenuBar();

    // On vérifie si Ctrl et Shift sont enfoncés
    bool control = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    bool shift   = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

    // Si on appuie sur 'S' (le 'false' empêche la répétition automatique de la touche)
    if (control && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (shift) {
            SaveSceneAs(); // Ctrl + Shift + S
        } else {
            SaveScene();   // Ctrl + S
        }
    }

    if (control && !shift && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        // Logique pour Ctrl+N (Nouvelle scène)
        m_ActiveScene = std::make_shared<Scene>();
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
        m_CurrentScenePath = ""; // On réinitialise le chemin car c'est une nouvelle scène
    }

    DrawProjectSettings();

    if (Project::GetActive() == nullptr) {
        m_HubPanel.OnImGuiRender();
    } else
    {
        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel->OnImGuiRender();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar);

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        // --- 1. BARRE D'OUTILS GIZMO ---
        ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
        if (ImGui::RadioButton("Translate (W)", m_GizmoType == ImGuizmo::OPERATION::TRANSLATE)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", m_GizmoType == ImGuizmo::OPERATION::ROTATE)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", m_GizmoType == ImGuizmo::OPERATION::SCALE)) m_GizmoType = ImGuizmo::OPERATION::SCALE;

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(20.0f, 0.0f));
        ImGui::SameLine();

        const char* modeStr = (m_GizmoMode == ImGuizmo::LOCAL) ? "Local Space" : "World Space";
        if (ImGui::Button(modeStr)) {
            m_GizmoMode = (m_GizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }

        // --- 2. GESTION DE LA TAILLE ---
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

        uint32_t textureID = m_ViewportFramebuffer->GetColorAttachmentRendererID();
        ImGui::Image((ImTextureID)(uintptr_t)textureID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        // --- DRAG & DROP TARGET (Viewport) ---
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* path = (const char*)payload->Data;
                std::filesystem::path filepath = path;

                if (filepath.extension() == ".obj" || filepath.extension() == ".fbx") {
                    auto entity = m_ActiveScene->CreateEntity(filepath.stem().string());

                    if (!entity.HasComponent<TransformComponent>()) {
                        entity.AddComponent<TransformComponent>();
                    }
                    if (!entity.HasComponent<ColorComponent>()) {
                        entity.AddComponent<ColorComponent>(glm::vec3(1.0f));
                    }
                    if (!entity.HasComponent<MeshComponent>()) {
                        entity.AddComponent<MeshComponent>();
                    }

                    auto& meshComp = entity.GetComponent<MeshComponent>();
                    meshComp.AssetPath = filepath.string();
                    meshComp.MeshData = ModelLoader::LoadModel(meshComp.AssetPath);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // --- 3. RENDU DU GIZMO 3D ---
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity && m_GizmoType != -1) {

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

            float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
            glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
            glm::mat4 view = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);

            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                                 (ImGuizmo::OPERATION)m_GizmoType,
                                 (ImGuizmo::MODE)m_GizmoMode,
                                 glm::value_ptr(transform));

            if (ImGuizmo::IsUsing()) {
                glm::vec3 scale;
                glm::quat rotation;
                glm::vec3 translation;
                glm::vec3 skew;
                glm::vec4 perspective;

                glm::decompose(transform, scale, rotation, translation, skew, perspective);

                tc.Location = translation;
                tc.Rotation = rotation;
                tc.Scale = scale;

                tc.RotationEuler = glm::degrees(glm::eulerAngles(rotation));
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
    EndDockspace();
}

void EditorLayer::CaptureViewportThumbnail(const std::string& projectPath) {
    if (!m_ViewportFramebuffer) return;

    auto& fb = m_ViewportFramebuffer;
    fb->Bind();

    int width = fb->GetSpecification().Width;
    int height = fb->GetSpecification().Height;

    std::vector<unsigned char> pixels(width * height * 4);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    fb->Unbind();

    for (size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }

    std::filesystem::path projectDir = std::filesystem::path(projectPath);
    if (std::filesystem::is_regular_file(projectDir)) {
        projectDir = projectDir.parent_path();
    }

    std::filesystem::path thumbDir = projectDir / ".ce_cache";
    if (!std::filesystem::exists(thumbDir)) {
        std::filesystem::create_directories(thumbDir);
    }

    std::string finalPath = (thumbDir / "thumbnail.png").string();
    stbi_flip_vertically_on_write(true);

    if (stbi_write_png(finalPath.c_str(), width, height, 4, pixels.data(), width * 4)) {
        std::cout << "[Engine] Thumbnail updated: " << finalPath << std::endl;
    }
}

void EditorLayer::DrawProjectSettings() {
    if (!m_ShowProjectSettings) return;

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Project Settings", &m_ShowProjectSettings)) {

        // --- COLONNE GAUCHE : CATÉGORIES ---
        ImGui::BeginChild("Categories", ImVec2(200, 0), true);
        static int selectedCategory = 0;
        if (ImGui::Selectable("Maps & Modes", selectedCategory == 0)) selectedCategory = 0;
        if (ImGui::Selectable("General", selectedCategory == 1)) selectedCategory = 1;
        ImGui::EndChild();

        ImGui::SameLine();

        // --- COLONNE DROITE : CONTENU ---
        ImGui::BeginChild("Details", ImVec2(0, 0), true);

        if (selectedCategory == 0) { // Maps & Modes
            ImGui::TextDisabled("DEFAULT MAPS");
            ImGui::Separator();
            ImGui::Spacing();

            auto project = Project::GetActive();
            if (project) {
                auto& config = project->GetConfig();

                ImGui::Text("Editor Startup Map");
                ImGui::SameLine(150.0f);

                char buffer[256];
                strncpy(buffer, config.StartScene.c_str(), sizeof(buffer));
                ImGui::InputText("##StartupMap", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);

                ImGui::SameLine();

                if (ImGui::Button("...##BrowseMap")) {
                    nfdchar_t* outPath = nullptr;
                    nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };

                    if (NFD::OpenDialog(outPath, filterItem, 1, project->GetProjectDirectory().string().c_str()) == NFD_OKAY) {
                        std::filesystem::path fullPath = outPath;
                        std::filesystem::path relPath = std::filesystem::relative(fullPath, project->GetProjectDirectory());

                        config.StartScene = relPath.string();

                        std::string projFileName = project->GetProjectDirectory().filename().string() + ".ceproj";
                        std::filesystem::path projFilePath = project->GetProjectDirectory() / projFileName;

                        Project::SaveActive(projFilePath);

                        NFD::FreePath(outPath);
                    }
                }
            }
        } else if (selectedCategory == 1) { // General
            ImGui::TextDisabled("PROJECT DETAILS");
            ImGui::Separator();
            ImGui::Spacing();

            // --- NOUVEAU : Formulaire de détails du projet ---
            auto project = Project::GetActive();
            if (project) {
                auto& config = project->GetConfig();

                char nameBuffer[256];
                strncpy(nameBuffer, config.Name.c_str(), sizeof(nameBuffer)); // Assume config.Name existe
                if (ImGui::InputText("Project Name", nameBuffer, sizeof(nameBuffer))) {
                    config.Name = std::string(nameBuffer);
                }

                char versionBuffer[256];
                strncpy(versionBuffer, config.Version.c_str(), sizeof(versionBuffer)); // Assume config.Version existe
                if (ImGui::InputText("Project Version", versionBuffer, sizeof(versionBuffer))) {
                    config.Version = std::string(versionBuffer);
                }

                ImGui::Spacing();
                if (ImGui::Button("Save Changes")) {
                    std::string projFileName = project->GetProjectDirectory().filename().string() + ".ceproj";
                    std::filesystem::path projFilePath = project->GetProjectDirectory() / projFileName;
                    Project::SaveActive(projFilePath);
                }
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorLayer::SaveScene() {
    if (!m_CurrentScenePath.empty()) {
        SceneSerializer serializer(m_ActiveScene);
        serializer.Serialize(m_CurrentScenePath.string());
        std::cout << "[Editor] Scene saved to " << m_CurrentScenePath << std::endl;
    }
    else {
        SaveSceneAs();
    }
}

void EditorLayer::SaveSceneAs() {
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };

    const char* defaultPath = nullptr;
    if (Project::GetActive()) {
        defaultPath = Project::GetContentDirectory().string().c_str();
    }

    if (NFD::SaveDialog(outPath, filterItem, 1, defaultPath, nullptr) == NFD_OKAY) {
        // --- NOUVEAU : GESTION DE L'EXTENSION ---
        std::filesystem::path filepath = outPath;

        // On s'assure que l'extension est bien présente
        if (filepath.extension() != ".cescene") {
            filepath += ".cescene";
        }

        m_CurrentScenePath = filepath;
        // ----------------------------------------

        SceneSerializer serializer(m_ActiveScene);
        serializer.Serialize(m_CurrentScenePath.string());

        NFD::FreePath(outPath);
        std::cout << "[Editor] Scene saved as " << m_CurrentScenePath << std::endl;
    }
}