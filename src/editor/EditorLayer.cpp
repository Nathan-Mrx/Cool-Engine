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
#include <math/Math.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <stb_image.h>
#include <glm/gtx/matrix_decompose.hpp>

void EditorLayer::OnAttach() {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false); // ImGui préfère le haut en haut !
    unsigned char* data = stbi_load("splash.png", &width, &height, &channels, 4);
    if (data) {
        glGenTextures(1, &m_SplashTextureID);
        glBindTexture(GL_TEXTURE_2D, m_SplashTextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }

    // 1. Initialisation du premier onglet vierge
    m_Tabs.push_back({ "Untitled", "", TabType::Scene, std::make_shared<Scene>(), false });
    m_ActiveTabIndex = 0;
    m_ActiveScene = m_Tabs[0].SceneContext;

    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();
    m_MaterialEditorPanel = std::make_unique<MaterialEditorPanel>();

    // 2. Connexion des callbacks du Content Browser !
    m_ContentBrowserPanel->OnSceneOpenCallback = [this](const std::filesystem::path& path) {
        OpenScene(path);
    };
    m_ContentBrowserPanel->OnPrefabOpenCallback = [this](const std::filesystem::path& path) {
        OpenPrefab(path);
    };
    m_ContentBrowserPanel->OnMaterialOpenCallback = [this](const std::filesystem::path& path) {
        OpenMaterial(path);
    };

    FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    m_ViewportFramebuffer = std::make_unique<Framebuffer>(fbSpec);

    if (Project::GetActive()) {
        // Optionnel : Tu pourras charger la scène de démarrage ici via OpenScene()
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
                SaveSceneAs();
            }

            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };
                if (NFD::OpenDialog(outPath, filterItem, 1, nullptr) == NFD_OKAY) {

                    OpenScene(outPath); // <-- ON UTILISE NOTRE FONCTION D'ONGLET !

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

        // --- Menu View pour la grille, les collisions et le rendu ---
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Grid", nullptr, &m_ShowGrid);
            ImGui::MenuItem("Show Collisions", nullptr, &m_ShowCollisions); // <-- NOUVEAU

            ImGui::Separator();

            if (ImGui::BeginMenu("Render Mode")) {
                if (ImGui::MenuItem("Lit", nullptr, m_RenderMode == 0)) m_RenderMode = 0;
                if (ImGui::MenuItem("Unlit", nullptr, m_RenderMode == 1)) m_RenderMode = 1;
                if (ImGui::MenuItem("Wireframe", nullptr, m_RenderMode == 2)) m_RenderMode = 2;
                ImGui::EndMenu();
            }
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

    // =========================================================================
    // NOUVEAU : LA BARRE D'ONGLETS GLOBALE (AU-DESSUS DU DOCKSPACE !)
    // =========================================================================
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Dummy(ImVec2(0, 4.0f)); // Petite marge

    if (ImGui::BeginTabBar("GlobalWorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (int i = 0; i < m_Tabs.size(); i++) {
            bool isOpen = true;
            ImGuiTabItemFlags flags = (m_ActiveTabIndex == i && m_ForceTabSelection) ? ImGuiTabItemFlags_SetSelected : 0;

            if (ImGui::BeginTabItem(m_Tabs[i].Name.c_str(), &isOpen, flags)) {
                if (m_ActiveTabIndex != i) {
                    m_ActiveTabIndex = i;

                    // On ne restaure la scène que si c'est un onglet de type scène
                    if (m_Tabs[i].Type == TabType::Scene) {
                        m_ActiveScene = m_Tabs[i].SceneContext;
                        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
                        m_SceneHierarchyPanel.SetSelectedEntity({});
                        m_SceneHierarchyPanel.SetIsPrefabScene(m_Tabs[i].IsPrefab);
                    }
                }
                ImGui::EndTabItem();
            }

            if (!isOpen) {
                CloseTab(i);
                i--; // Ajustement de l'index car un onglet vient d'être supprimé
            }
        }
        ImGui::EndTabBar();
    }
    m_ForceTabSelection = false;
    ImGui::PopStyleVar();
    // =========================================================================

    ImGuiID dockspace_id = ImGui::GetID("MyEngineDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

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

        // --- On ancre aussi le Material Editor au centre par défaut ! ---
        ImGui::DockBuilderDockWindow("Material Editor", dock_id_main);

        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock_id_main);
        if (node) node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

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
    // --- LECTURE DE LA FILE D'ATTENTE ---
    std::filesystem::path pending = Project::ConsumePendingProject();
    if (!pending.empty()) {
        m_PendingProjectPath = pending;
        ProjectCompiler::Start(pending.parent_path());
    }

    // Si on est en train de compiler, on gèle l'Update du reste du moteur !
    if (ProjectCompiler::IsCompiling() || ProjectCompiler::HasFinished()) {
        return;
    }

    // --- 1. DÉTECTION DE CHARGEMENT DE PROJET AUTOMATIQUE ---
    static std::shared_ptr<Project> s_LastProject = nullptr;
    auto currentProject = Project::GetActive();

    if (currentProject && currentProject != s_LastProject) {
        s_LastProject = currentProject;

        std::filesystem::path scenePath = currentProject->GetProjectDirectory() / currentProject->GetConfig().StartScene;

        if (std::filesystem::exists(scenePath)) {
            OpenScene(scenePath);

            // Nettoyage : Si le premier onglet est "Untitled" et qu'on vient de charger le projet, on le ferme
            if (m_Tabs.size() > 1 && m_Tabs[0].Name == "Untitled") {
                CloseTab(0);
            }

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

    // --- 3. GESTION DE L'ÉTAT DU JEU ---
    switch (m_SceneState) {
        case SceneState::Edit: {
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
                break;
            }

        case SceneState::Play: {
            m_ActiveScene->OnUpdateScripts(ts);
            m_ActiveScene->OnUpdatePhysics(ts);
            break;
        }

        case SceneState::Pause: {
            // On ne fait rien
            break;
        }
    }

    // --- 4. RENDU DE LA SCÈNE ---
    m_ViewportFramebuffer->Bind();
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    Renderer::Clear();

    m_ViewportFramebuffer->ClearAttachment(1, -1);

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;
    float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;

    if (m_SceneState == SceneState::Edit) {
        // --- MODE ÉDITION : On utilise la caméra de l'éditeur ---
        projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
        view = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);
        cameraPosition = m_EditorCamera.Position;
    } else {
        // --- MODE PLAY : On cherche la caméra du joueur ---
        auto cameraView = m_ActiveScene->m_Registry.view<TransformComponent, CameraComponent>();
        bool cameraFound = false;

        for (auto entityID : cameraView) {
            Entity entity{ entityID, m_ActiveScene.get() };
            auto& camera = entity.GetComponent<CameraComponent>();

            if (camera.Primary) {
                // --- LE FIX : On décompose la position GLOBALE de la caméra ---
                glm::mat4 globalTransform = m_ActiveScene->GetWorldTransform(entity);
                glm::vec3 globalPos, globalScale;
                glm::quat globalRot;
                Math::DecomposeTransform(globalTransform, globalPos, globalRot, globalScale);

                projection = glm::perspectiveLH(glm::radians(camera.FOV), aspectRatio, camera.NearClip, camera.FarClip);

                // On utilise la rotation globale pour trouver l'avant et le haut
                glm::vec3 forward = glm::normalize(globalRot * glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 up      = glm::normalize(globalRot * glm::vec3(0.0f, 0.0f, 1.0f));

                view = glm::lookAtLH(globalPos, globalPos + forward, up);
                cameraPosition = globalPos;

                cameraFound = true;
                break;
            }
        }

        // --- FALLBACK : Si le niveau n'a pas de CameraComponent, on garde la vue Éditeur ---
        if (!cameraFound) {
            projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
            view = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);
            cameraPosition = m_EditorCamera.Position;
        }
    }

    // On envoie la bonne vue au moteur de rendu !
    Renderer::BeginScene(view, projection, cameraPosition);

    Renderer::DrawGrid(m_ShowGrid);
    Renderer::RenderScene(m_ActiveScene.get(), m_RenderMode);

    // --- NOUVEAU : RENDU DES COLLISIONS (Avec Hiérarchie) ---
    if (m_ShowCollisions) {
        auto view = m_ActiveScene->m_Registry.view<BoxColliderComponent>();
        for (auto entityID : view) {
            Entity entity{ entityID, m_ActiveScene.get() };
            auto& bc = entity.GetComponent<BoxColliderComponent>();

            // On récupère la vraie matrice globale
            glm::mat4 globalTransform = m_ActiveScene->GetWorldTransform(entity);

            // On ajoute l'offset et la taille locale par-dessus
            glm::mat4 boxTransform = globalTransform
                                   * glm::translate(glm::mat4(1.0f), bc.Offset)
                                   * glm::scale(glm::mat4(1.0f), bc.HalfSize);

            Renderer::DrawDebugBox(boxTransform, glm::vec3(0.1f, 0.9f, 0.1f));
        }
    }

    auto lightView = m_ActiveScene->m_Registry.view<DirectionalLightComponent>();
    for (auto entityID : lightView) {
        Entity entity{ entityID, m_ActiveScene.get() };

        // --- LE FIX : Position et Rotation Globales de la lumière ---
        glm::mat4 globalTransform = m_ActiveScene->GetWorldTransform(entity);
        glm::vec3 globalPos, globalScale;
        glm::quat globalRot;
        Math::DecomposeTransform(globalTransform, globalPos, globalRot, globalScale);

        // On crée un transform temporaire juste pour utiliser tes fonctions mathématiques
        TransformComponent tempTc;
        tempTc.Location = globalPos;
        tempTc.Rotation = globalRot;

        glm::vec3 lightDirection = glm::normalize(globalRot * glm::vec3(0.0f, -1.0f, 0.0f));

        Renderer::DrawDebugArrow(
            tempTc.Location,
            lightDirection,
            tempTc.GetRightVector(),
            tempTc.GetForwardVector(),
            glm::vec3(1.0f, 0.9f, 0.1f),
            view, projection,
            50.0f
        );
    }

    Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    if (selectedEntity) {

        // 1. On collecte tous les meshes de l'entité et de ses enfants
        std::vector<std::pair<glm::mat4, MeshComponent*>> meshesToOutline;

        // Fonction récursive
        auto collectMeshes = [&](Entity e, auto& self) -> void {
            if (!e) return;

            if (e.HasComponent<MeshComponent>()) {
                // On stocke la matrice GLOBALE et le mesh
                meshesToOutline.push_back({ m_ActiveScene->GetWorldTransform(e), &e.GetComponent<MeshComponent>() });
            }

            if (e.HasComponent<RelationshipComponent>()) {
                entt::entity childID = e.GetComponent<RelationshipComponent>().FirstChild;
                while (childID != entt::null) {
                    Entity child{ childID, m_ActiveScene.get() };
                    self(child, self); // On descend dans l'arbre
                    childID = child.GetComponent<RelationshipComponent>().NextSibling;
                }
            }
        };

        // On lance la collecte depuis la racine sélectionnée
        collectMeshes(selectedEntity, collectMeshes);

        if (!meshesToOutline.empty()) {
            // Étape 1 : Le masque (On dessine TOUS les masques d'abord)
            for (auto& [transform, mesh] : meshesToOutline) {
                if (mesh->MeshData) {
                    Renderer::BeginOutlineMask(transform);
                    mesh->MeshData->Draw();
                }
            }

            // Étape 2 : L'outline via le fil de fer épais (Sur TOUT le groupe)
            for (auto& [transform, mesh] : meshesToOutline) {
                if (mesh->MeshData) {
                    Renderer::BeginOutlineDraw(transform, glm::vec3(1.0f, 0.5f, 0.0f));
                    mesh->MeshData->Draw();
                }
            }

            // Étape 3 : Nettoyage
            Renderer::EndOutline();
        }
    }

    // --- NOUVEAU : RENDU DU FRUSTUM AVEC HIERARCHIE ---
    if (m_SceneState == SceneState::Edit && selectedEntity && selectedEntity.HasComponent<CameraComponent>()) {
        auto& cc = selectedEntity.GetComponent<CameraComponent>();

        // 1. On récupère la matrice globale
        glm::mat4 globalTransform = m_ActiveScene->GetWorldTransform(selectedEntity);
        glm::vec3 globalPos, globalScale;
        glm::quat globalRot;
        Math::DecomposeTransform(globalTransform, globalPos, globalRot, globalScale);

        // 2. On recrée les matrices exactes
        float aspect = m_ViewportSize.x / m_ViewportSize.y;
        glm::mat4 camProj = glm::perspectiveLH(glm::radians(cc.FOV), aspect, cc.NearClip, cc.FarClip);

        glm::vec3 forward = glm::normalize(globalRot * glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 up      = glm::normalize(globalRot * glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 camView = glm::lookAtLH(globalPos, globalPos + forward, up);

        // 3. L'inverse de Projection * Vue globale
        glm::mat4 frustumTransform = glm::inverse(camProj * camView);

        Renderer::DrawDebugBox(frustumTransform, glm::vec3(1.0f, 1.0f, 1.0f));
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

    // --- LE SPLASH SCREEN DE COMPILATION ---
    if (ProjectCompiler::IsCompiling() || ProjectCompiler::HasFinished()) {
        DrawSplashScreen();
        EndDockspace();
        return; // ON BLOQUE L'AFFICHAGE DU RESTE DE L'ÉDITEUR !
    }

    DrawMenuBar();

    bool isSceneTabActive = (!m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].Type == TabType::Scene);
    if (isSceneTabActive) {
        UI_Toolbar();
    }

    // --- RACCOURCIS CLAVIER ---
    bool control = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    bool shift   = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

    if (control && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (shift) SaveSceneAs();
        else SaveScene();
    }

    if (control && !shift && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        // Nouvelle scène = Nouvel onglet !
        auto newScene = std::make_shared<Scene>();
        m_Tabs.push_back({ "Untitled", "", TabType::Scene, newScene, false });
        m_ActiveTabIndex = m_Tabs.size() - 1;
        m_ActiveScene = newScene;
        m_ForceTabSelection = true;
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    DrawProjectSettings();

    // --- PANNEAUX DE L'ÉDITEUR ---
    if (Project::GetActive() == nullptr) {
        m_HubPanel.OnImGuiRender();
    } else {
        // Le Content Browser est commun à tous les modes de travail !
        m_ContentBrowserPanel->OnImGuiRender();

        if (!m_Tabs.empty()) {
            auto& activeTab = m_Tabs[m_ActiveTabIndex];

            if (activeTab.Type == TabType::Scene) {
                // --- MODE SCÈNE ---
                m_SceneHierarchyPanel.OnImGuiRender();

                // L'ancienne fenêtre Viewport (Sans le bloc TabBar que tu as effacé !)
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
                ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::BeginChild("RenderViewport", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                m_ViewportFocused = ImGui::IsWindowFocused();
                m_ViewportHovered = ImGui::IsWindowHovered();

                // --- 3. LA BARRE D'OUTILS GIZMO ---
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
                if (ImGui::Button(modeStr)) m_GizmoMode = (m_GizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

                // --- 4. GESTION DE LA TAILLE ET CLICS (RAYCAST) ---
                ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
                m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_ViewportHovered && !ImGuizmo::IsOver()) {
                    ImVec2 mousePos = ImGui::GetMousePos();
                    ImVec2 windowPos = ImGui::GetWindowPos();
                    ImVec2 viewportMin = ImGui::GetWindowContentRegionMin();

                    float mouseX = mousePos.x - (windowPos.x + viewportMin.x);
                    float mouseY = mousePos.y - (windowPos.y + viewportMin.y);
                    mouseY = m_ViewportSize.y - mouseY; // Inversion Y pour OpenGL

                    if (mouseX >= 0 && mouseY >= 0 && mouseX < m_ViewportSize.x && mouseY < m_ViewportSize.y) {
                        m_ViewportFramebuffer->Bind();
                        int pixelData = m_ViewportFramebuffer->ReadPixel(1, (int)mouseX, (int)mouseY);
                        m_ViewportFramebuffer->Unbind();

                        if (pixelData != -1) {
                            if (m_ActiveScene->m_Registry.valid((entt::entity)pixelData)) {
                                Entity clickedEntity((entt::entity)pixelData, m_ActiveScene.get());

                                // --- LE FIX UNREAL (Sélection globale de l'Actor) ---
                                // Si on clique sur un enfant de Prefab, on sélectionne sa racine !
                                Entity prefabRoot = m_SceneHierarchyPanel.GetPrefabRoot(clickedEntity);
                                if (prefabRoot) {
                                    m_SceneHierarchyPanel.SetSelectedEntity(prefabRoot);
                                } else {
                                    m_SceneHierarchyPanel.SetSelectedEntity(clickedEntity);
                                }
                            }
                        } else {
                            m_SceneHierarchyPanel.SetSelectedEntity({});
                        }
                    }
                }

                // --- 5. L'IMAGE OPENGL ---
                uint32_t textureID = m_ViewportFramebuffer->GetColorAttachmentRendererID();
                ImGui::Image((ImTextureID)(uintptr_t)textureID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

                // --- 6. DRAG & DROP TARGET (Modèles 3D) ---
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* path = (const char*)payload->Data;
                        std::filesystem::path filepath = path;

                        if (filepath.extension() == ".obj" || filepath.extension() == ".fbx") {
                            auto entity = m_ActiveScene->CreateEntity(filepath.stem().string());

                            if (!entity.HasComponent<TransformComponent>()) entity.AddComponent<TransformComponent>();
                            if (!entity.HasComponent<ColorComponent>()) entity.AddComponent<ColorComponent>(glm::vec3(1.0f));
                            if (!entity.HasComponent<MeshComponent>()) entity.AddComponent<MeshComponent>();

                            auto& meshComp = entity.GetComponent<MeshComponent>();
                            meshComp.AssetPath = filepath.string();
                            meshComp.MeshData = ModelLoader::LoadModel(meshComp.AssetPath);
                        }
                        else if (filepath.extension() == ".ceprefab") {
                            SceneSerializer serializer(m_ActiveScene);
                            serializer.DeserializePrefab(filepath.string());
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // --- 7. IMGUIZMO (MANIPULATION 3D) ---
                if (m_SceneHierarchyPanel.GetSelectedEntity() && m_GizmoType != -1) {
                    Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

                    if (m_ViewportSize.y > 0.0f) {
                        float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
                        glm::mat4 cameraProjection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
                        glm::mat4 cameraView = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);

                        glm::mat4 globalTransform = m_ActiveScene->GetWorldTransform(selectedEntity);

                        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                                             (ImGuizmo::OPERATION)m_GizmoType, (ImGuizmo::MODE)m_GizmoMode, glm::value_ptr(globalTransform));

                        if (ImGuizmo::IsUsing()) {
                            glm::mat4 localTransform = globalTransform;

                            if (selectedEntity.HasComponent<RelationshipComponent>()) {
                                entt::entity parentID = selectedEntity.GetComponent<RelationshipComponent>().Parent;
                                if (parentID != entt::null) {
                                    Entity parent{ parentID, m_ActiveScene.get() };
                                    glm::mat4 parentGlobal = m_ActiveScene->GetWorldTransform(parent);
                                    localTransform = glm::inverse(parentGlobal) * globalTransform;
                                }
                            }

                            glm::vec3 translation, scale;
                            glm::quat rotation;
                            Math::DecomposeTransform(localTransform, translation, rotation, scale);

                            auto& tc = selectedEntity.GetComponent<TransformComponent>();
                            tc.Location = translation;
                            tc.Rotation = rotation;
                            tc.RotationEuler = glm::degrees(glm::eulerAngles(rotation));
                            tc.Scale = scale;
                        }
                    }
                }

                ImGui::EndChild();
                ImGui::End();
                ImGui::PopStyleVar();

            } else if (activeTab.Type == TabType::Material) {
                // --- MODE MATÉRIAU ---
                // On affiche uniquement l'éditeur nodal ! La Hiérarchie et le Viewport disparaissent.
                bool open = true;
                m_MaterialEditorPanel->OnImGuiRender(open);
            }
        }
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
    if (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < m_Tabs.size()) {
        auto& activeTab = m_Tabs[m_ActiveTabIndex];
        if (!activeTab.Filepath.empty()) {
            SceneSerializer serializer(activeTab.SceneContext);
            serializer.Serialize(activeTab.Filepath.string());
            std::cout << "[Editor] Saved " << (activeTab.IsPrefab ? "Prefab" : "Scene") << " to " << activeTab.Filepath << std::endl;
        } else {
            SaveSceneAs();
        }
    }
}

void EditorLayer::SaveSceneAs() {
    if (m_ActiveTabIndex < 0 || m_ActiveTabIndex >= m_Tabs.size()) return;
    auto& activeTab = m_Tabs[m_ActiveTabIndex];

    nfdchar_t* outPath = nullptr;
    const char* filterName = activeTab.IsPrefab ? "Cool Engine Prefab" : "Cool Engine Scene";
    const char* filterExt = activeTab.IsPrefab ? "ceprefab" : "cescene";
    nfdfilteritem_t filterItem[1] = { { filterName, filterExt } };

    if (NFD::SaveDialog(outPath, filterItem, 1, nullptr, nullptr) == NFD_OKAY) {
        std::filesystem::path filepath = outPath;
        std::string ext = activeTab.IsPrefab ? ".ceprefab" : ".cescene";

        if (filepath.extension() != ext) {
            filepath += ext;
        }

        activeTab.Filepath = filepath;
        activeTab.Name = filepath.filename().string();
        if (activeTab.IsPrefab) activeTab.Name = "[Prefab] " + activeTab.Name;

        SceneSerializer serializer(activeTab.SceneContext);
        serializer.Serialize(activeTab.Filepath.string());

        NFD::FreePath(outPath);
    }
}

void EditorLayer::UI_Toolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    auto& colors = ImGui::GetStyle().Colors;
    const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
    const auto& buttonActive = colors[ImGuiCol_ButtonActive];
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));

    ImGui::Begin("##Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float size = ImGui::GetWindowHeight() - 4.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 1.5f));

    bool isPlaying = m_SceneState == SceneState::Play || m_SceneState == SceneState::Pause;
    const char* playStopIcon = isPlaying ? "STOP" : "PLAY";

    if (ImGui::Button(playStopIcon, ImVec2(size * 2.0f, size))) {
        if (m_SceneState == SceneState::Edit)
            OnScenePlay();
        else if (isPlaying)
            OnSceneStop();
    }

    ImGui::SameLine();

    if (ImGui::Button("PAUSE", ImVec2(size * 2.0f, size))) {
        OnScenePause();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::End();
}

void EditorLayer::OnScenePlay() {
    m_SceneState = SceneState::Play;

    // Sauvegarde temporaire pour figer l'état initial
    SceneSerializer serializer(m_ActiveScene);
    serializer.Serialize("TempSceneBackup.cescene");

    m_ActiveScene->OnScriptStart();
    m_ActiveScene->OnPhysicsStart();

    std::cout << "[Editor] Entered PLAY mode." << std::endl;
}

void EditorLayer::OnSceneStop() {
    m_SceneState = SceneState::Edit;

    m_ActiveScene->OnScriptStop();
    m_ActiveScene->OnPhysicsStop();

    // On recharge la scène pour annuler toutes les destructions/mouvements
    m_ActiveScene = std::make_shared<Scene>();
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);

    SceneSerializer serializer(m_ActiveScene);
    serializer.Deserialize("TempSceneBackup.cescene");

    std::cout << "[Editor] Entered EDIT mode (Scene Restored)." << std::endl;
}

void EditorLayer::OnScenePause() {
    if (m_SceneState == SceneState::Play) {
        m_SceneState = SceneState::Pause;
    } else if (m_SceneState == SceneState::Pause) {
        m_SceneState = SceneState::Play;
    }
}

void EditorLayer::OpenScene(const std::filesystem::path& path) {
    for (int i = 0; i < m_Tabs.size(); i++) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i; m_ForceTabSelection = true; return;
        }
    }
    auto newScene = std::make_shared<Scene>();
    SceneSerializer serializer(newScene);
    serializer.Deserialize(path.string());

    m_Tabs.push_back({ path.filename().string(), path, TabType::Scene, newScene, false });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ActiveScene = newScene;
    m_ForceTabSelection = true;
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
}

void EditorLayer::OpenPrefab(const std::filesystem::path& path) {
    for (int i = 0; i < m_Tabs.size(); i++) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i; m_ForceTabSelection = true; return;
        }
    }
    auto newPrefabScene = std::make_shared<Scene>();
    SceneSerializer serializer(newPrefabScene);
    serializer.Deserialize(path.string());

    m_Tabs.push_back({ "[Prefab] " + path.filename().string(), path, TabType::Scene, newPrefabScene, true });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ActiveScene = newPrefabScene;
    m_ForceTabSelection = true;
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
}

void EditorLayer::CloseTab(int index) {
    if (index < 0 || index >= m_Tabs.size()) return;
    m_Tabs.erase(m_Tabs.begin() + index);

    if (m_Tabs.empty()) m_Tabs.push_back({ "Untitled", "", TabType::Scene, std::make_shared<Scene>(), false });
    if (m_ActiveTabIndex >= m_Tabs.size()) m_ActiveTabIndex = m_Tabs.size() - 1;

    // Fix sécurité
    if (m_Tabs[m_ActiveTabIndex].Type == TabType::Scene) {
        m_ActiveScene = m_Tabs[m_ActiveTabIndex].SceneContext;
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }
}

void EditorLayer::DrawSplashScreen() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("SplashScreenOverlay", nullptr, flags)) {
        ImVec2 windowSize = ImGui::GetWindowSize();

        // 1. Affichage de l'image (Centrée en haut)
        if (m_SplashTextureID) {
            float imgWidth = 544.0f; float imgHeight = 864.0f; // Tailles originales
            float ratio = std::min(windowSize.x / imgWidth, (windowSize.y - 250) / imgHeight);
            ImVec2 size(imgWidth * ratio, imgHeight * ratio);
            ImGui::SetCursorPos(ImVec2((windowSize.x - size.x) * 0.5f, 20.0f));
            ImGui::Image((ImTextureID)(uintptr_t)m_SplashTextureID, size);
        }

        // 2. La console de compilation (En bas)
        ImGui::SetCursorPos(ImVec2(20.0f, windowSize.y - 220.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
        ImGui::BeginChild("CompilationLogs", ImVec2(windowSize.x - 40.0f, 200.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        for (const auto& log : ProjectCompiler::GetLogs()) {
            ImGui::TextUnformatted(log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // 3. Traitement de la fin !
        if (ProjectCompiler::HasFinished()) {
            if (ProjectCompiler::GetResult()) {
                // SUCCÈS : On charge le .so et on rentre dans l'éditeur !
                ProjectCompiler::Reset();
                Project::Load(m_PendingProjectPath);
            } else {
                // ÉCHEC : On reste bloqué sur l'écran d'erreur
                ImGui::SetCursorPos(ImVec2(20.0f, windowSize.y - 260.0f));
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "COMPILATION FAILED ! Veuillez corriger votre code C++.");
                ImGui::SameLine();
                if (ImGui::Button("Cancel & Return to Hub")) {
                    ProjectCompiler::Reset();
                    m_PendingProjectPath = "";
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorLayer::OpenMaterial(const std::filesystem::path& path) {
    for (int i = 0; i < m_Tabs.size(); i++) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i; m_ForceTabSelection = true; return;
        }
    }
    // On ajoute un onglet spécifiquement typé "Material"
    m_Tabs.push_back({ path.filename().string(), path, TabType::Material, nullptr, false });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ForceTabSelection = true;
}