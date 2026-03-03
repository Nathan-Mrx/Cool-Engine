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

            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                nfdchar_t* outPath = nullptr;
                // Le filtre "cescene" garantit la bonne extension
                nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };
                if (NFD::SaveDialog(outPath, filterItem, 1, nullptr, "Untitled.cescene") == NFD_OKAY) {
                    SceneSerializer serializer(m_ActiveScene);
                    serializer.Serialize(outPath);
                    NFD::FreePath(outPath);
                }
            }

            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };
                if (NFD::OpenDialog(outPath, filterItem, 1, nullptr) == NFD_OKAY) {
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
        // ... Menu View
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
    // On vérifie si le dockspace est déjà configuré. Si non, on crée le layout.
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr || ImGui::DockBuilderGetNode(dockspace_id)->ChildNodes[0] == 0) {

        // On nettoie tout noeud existant pour repartir sur une base propre
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_id_main = dockspace_id;

        // 1. On sépare la GAUCHE pour la Hiérarchie (20% de l'écran)
        ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Left, 0.20f, nullptr, &dock_id_main);

        // 2. On sépare la DROITE pour l'Inspector (25% de ce qu'il reste)
        ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Right, 0.25f, nullptr, &dock_id_main);

        // 3. Dans la zone centrale restante, on sépare le BAS pour le Content Browser (30% de hauteur)
        ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Down, 0.30f, nullptr, &dock_id_main);

        // 4. On assigne les fenêtres aux nœuds via leurs noms exacts
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
    // Ici, on décharge proprement le projet du moteur
    Project::Unload(); //

    // Optionnel : On peut aussi réinitialiser la scène ici
    m_ActiveScene = std::make_shared<Scene>();
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
}

void EditorLayer::OnUpdate(float ts) {
    // --- 1. DÉTECTION DE CHARGEMENT DE PROJET AUTOMATIQUE ---
    static std::shared_ptr<Project> s_LastProject = nullptr;
    auto currentProject = Project::GetActive();

    // Si on vient d'ouvrir un projet depuis le Hub
    if (currentProject && currentProject != s_LastProject) {
        s_LastProject = currentProject;

        // 1. Initialisation d'une scène vierge propre
        m_ActiveScene = std::make_shared<Scene>();
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        // 2. Récupération du chemin de la StartScene
        std::filesystem::path scenePath = currentProject->GetProjectDirectory() / currentProject->GetConfig().StartScene;

        // 3. Chargement silencieux
        if (std::filesystem::exists(scenePath)) {
            SceneSerializer serializer(m_ActiveScene);
            serializer.Deserialize(scenePath.string());
            std::cout << "[Editor] Loaded default scene: " << currentProject->GetConfig().StartScene << std::endl;
        } else {
            std::cout << "[Editor] Warning: Default scene not found at " << scenePath << std::endl;
        }
    }

    // Si aucun projet n'est actif (on est sur le Hub), on ne calcule pas la 3D
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

    // On ne change d'outil QUE si on ne vole pas avec la caméra (Pas de clic droit)
    if (m_ViewportFocused) {
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }
    }

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {

        // 1. Calcul de la rotation
        glm::vec2 mousePos = { (float)mouseX, (float)mouseY };
        glm::vec2 delta = (mousePos - m_LastMousePosition) * 0.003f;
        m_LastMousePosition = mousePos;

        m_EditorCamera.Yaw += delta.x;
        m_EditorCamera.Pitch -= delta.y;

        m_EditorCamera.Pitch = glm::clamp(m_EditorCamera.Pitch, glm::radians(-89.0f), glm::radians(89.0f));

        // 2. Calcul du vecteur Front
        glm::vec3 front;
        front.x = cos(m_EditorCamera.Yaw) * cos(m_EditorCamera.Pitch);
        front.y = sin(m_EditorCamera.Yaw) * cos(m_EditorCamera.Pitch);
        front.z = sin(m_EditorCamera.Pitch);
        m_EditorCamera.Front = glm::normalize(front);

        // 3. Déplacement avec gestion du Sprint
        float baseSpeed = 500.0f; // 5 m/s
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            baseSpeed *= 4.0f; // Sprint à 20 m/s
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
    // --------------------------------------------------

    // --- 4. RENDU DE LA SCÈNE ---
    m_ViewportFramebuffer->Bind();
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    Renderer::Clear();

    // Calcul des matrices
    float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
    glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
    glm::mat4 view = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);

    Renderer::BeginScene(view, projection, m_EditorCamera.Position);
    Renderer::DrawGrid(m_ShowGrid);
    Renderer::RenderScene(m_ActiveScene.get());

    auto lightView = m_ActiveScene->m_Registry.view<TransformComponent, DirectionalLightComponent>();
    for (auto entity : lightView) {
        auto [transform, light] = lightView.get<TransformComponent, DirectionalLightComponent>(entity);

        // 1. On calcule la VRAIE direction de la lumière (vers le bas en local, comme dans le shader)
        glm::vec3 lightDirection = glm::normalize(transform.Rotation * glm::vec3(0.0f, -1.0f, 0.0f));

        // 2. On récupère deux axes perpendiculaires pour dessiner la pointe de la flèche
        glm::vec3 rightAxis = transform.GetRightVector();
        glm::vec3 upAxis    = transform.GetForwardVector();

        // 3. On dessine la flèche
        Renderer::DrawDebugArrow(
            transform.Location,
            lightDirection, // La flèche pointe maintenant là où la lumière frappe !
            rightAxis,
            upAxis,
            glm::vec3(1.0f, 0.9f, 0.1f), // Jaune
            view, projection,
            50.0f
        );
    }

    Renderer::EndScene();

    // --- 5. LOGIQUE DE FERMETURE ET CAPTURE THUMBNAIL ---
    bool isAppClosing = glfwWindowShouldClose(Application::Get().GetWindow());

    if (m_RequestCloseProject || isAppClosing) {
        if (currentProject) {
            // On capture la thumbnail tant que le projet est actif et le contexte vivant
            CaptureViewportThumbnail(currentProject->GetProjectDirectory().string());

            // Si c'est une fermeture manuelle du projet (via le menu), on décharge.
            // Si l'app se ferme, on laisse le moteur s'arrêter proprement sans Unload forcé.
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

    // --- L'APPEL MANQUANT EST ICI ---
    // On dessine la fenêtre des paramètres si le flag m_ShowProjectSettings est à true
    DrawProjectSettings();

    if (Project::GetActive() == nullptr) {
        m_HubPanel.OnImGuiRender();
    } else
    {
        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel->OnImGuiRender();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 }); // Enlève les bordures moches
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar);

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        // --- 1. BARRE D'OUTILS GIZMO ---
        ImGui::SetCursorPos(ImVec2(10.0f, 10.0f)); // Petit padding interne
        if (ImGui::RadioButton("Translate (W)", m_GizmoType == ImGuizmo::OPERATION::TRANSLATE)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", m_GizmoType == ImGuizmo::OPERATION::ROTATE)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", m_GizmoType == ImGuizmo::OPERATION::SCALE)) m_GizmoType = ImGuizmo::OPERATION::SCALE;

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(20.0f, 0.0f)); // Adds a nice visual gap
        ImGui::SameLine();

        // The button displays the current state and flips it when clicked
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

                // Si c'est un modèle 3D, on spawn une entité !
                if (filepath.extension() == ".obj" || filepath.extension() == ".fbx") {
                    auto entity = m_ActiveScene->CreateEntity(filepath.stem().string());

                    // Sécurité : on n'ajoute les composants que s'ils n'existent pas déjà
                    if (!entity.HasComponent<TransformComponent>()) {
                        entity.AddComponent<TransformComponent>();
                    }
                    if (!entity.HasComponent<ColorComponent>()) {
                        entity.AddComponent<ColorComponent>(glm::vec3(1.0f));
                    }
                    if (!entity.HasComponent<MeshComponent>()) {
                        entity.AddComponent<MeshComponent>();
                    }

                    // On récupère le composant pour le modifier (qu'il vienne d'être créé ou non)
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

            // Configuration de base d'ImGuizmo
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            // On cale parfaitement le Gizmo sur la fenêtre de ton Viewport
            ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

            // Récupération des matrices de ta caméra
            float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
            glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
            glm::mat4 view = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);

            // Récupération de la matrice de l'entité
            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            // L'appel magique qui affiche les flèches et gère la souris !
            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                                 (ImGuizmo::OPERATION)m_GizmoType,
                                 (ImGuizmo::MODE)m_GizmoMode,
                                 glm::value_ptr(transform));

            // Si l'utilisateur est en train de tirer sur une flèche
            // Si l'utilisateur est en train de tirer sur une flèche
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

                // On force le cache UI à s'aligner sur la nouvelle rotation du Gizmo
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
    // On s'assure de lire le buffer de couleur actuel
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    fb->Unbind();

    // --- FIX : FORCER L'OPACITÉ ---
    // On parcourt les pixels (R, G, B, A) et on force le A à 255
    for (size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }

    // --- FIX : CHEMIN COHÉRENT ---
    // On part du répertoire racine du projet
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
        // Tu pourras ajouter "Physics", "Input", etc. ici plus tard
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
                auto& config = project->GetConfig(); // Nécessite ProjectConfig& GetConfig() dans Project.h

                ImGui::Text("Editor Startup Map");
                ImGui::SameLine(150.0f); // Aligne le champ texte

                // Affichage du chemin actuel
                char buffer[256];
                strncpy(buffer, config.StartScene.c_str(), sizeof(buffer));
                ImGui::InputText("##StartupMap", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);

                ImGui::SameLine();

                // Bouton pour parcourir
                if (ImGui::Button("...##BrowseMap")) {
                    nfdchar_t* outPath = nullptr;
                    nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };

                    // On ouvre NFD directement dans le dossier Content du projet
                    if (NFD::OpenDialog(outPath, filterItem, 1, project->GetProjectDirectory().string().c_str()) == NFD_OKAY) {

                        // On calcule le chemin relatif pour la portabilité (très important si tu partages ton projet)
                        std::filesystem::path fullPath = outPath;
                        std::filesystem::path relPath = std::filesystem::relative(fullPath, project->GetProjectDirectory());

                        config.StartScene = relPath.string();

                        // Sauvegarde immédiate du fichier .ceproj
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
            // Inputs pour le nom du projet, la version, etc.
        }

        ImGui::EndChild();
    }
    ImGui::End();
}