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
#include "panels/MaterialInstanceEditorPanel.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <stb_image.h>
#include <glm/gtx/matrix_decompose.hpp>

#include "AssetRegistry.h"
#include "EditorCommands.h"
#include "UndoManager.h"
#include "UITheme.h"
#include <fstream>
#include <nlohmann/json.hpp>


void EditorLayer::OnAttach() {
    LoadEditorPreferences();

    // --- On utilise notre chargeur universel ---
    m_SplashTextureID = TextureLoader::LoadTexture("splash.png");

    AssetRegistry::RegisterAllAssets();

    // 1. Initialisation pure de la Scène
    m_ActiveScene = std::make_shared<Scene>();
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();

    // 2. Callbacks du Content Browser
    m_ContentBrowserPanel->SetSceneOpenCallback([this](const std::filesystem::path& path) { OpenScene(path); });
    m_ContentBrowserPanel->SetPrefabOpenCallback([this](const std::filesystem::path& path) { OpenPrefab(path); });
    m_ContentBrowserPanel->SetMaterialOpenCallback([this](const std::filesystem::path& path) { OpenMaterial(path); });
    m_ContentBrowserPanel->SetMaterialInstanceOpenCallback([this](const std::filesystem::path& path) { OpenMaterialInstance(path); });

    FramebufferSpecification fbSpec{};
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    m_ViewportFramebuffer = Framebuffer::Create(fbSpec);
}

void EditorLayer::OnDetach() {
    // Nettoyage si nécessaire
    SaveEditorPreferences();
}

void EditorLayer::DrawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                auto newScene = std::make_shared<Scene>();
                m_Tabs.push_back({ "New Scene", "", TabType::Scene, newScene, nullptr });
                m_ActiveTabIndex = m_Tabs.size() - 1;
                m_ForceTabSelection = true;

                m_ActiveScene = newScene;
                m_SceneHierarchyPanel.SetContext(m_ActiveScene);
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };
                if (NFD::OpenDialog(outPath, filterItem, 1, nullptr) == NFD_OKAY) {
                    OpenScene(outPath);
                    NFD::FreePath(outPath);
                }
            }

            ImGui::Separator();

            // La sauvegarde dépend de l'onglet actif

            if (!m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].CustomEditor != nullptr) {
                m_Tabs[m_ActiveTabIndex].CustomEditor->OnImGuiMenuFile();
            } else {
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) SaveSceneAs();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) m_RequestCloseProject = true;
            if (ImGui::MenuItem("Exit")) Application::Get().Close();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) UndoManager::Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) UndoManager::Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Project Settings")) m_ShowProjectSettings = true;
            if (ImGui::MenuItem("Editor Preferences")) m_ShowEditorPreferences = true; // <-- AJOUT ICI
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            bool isSceneTab = !m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].Type == TabType::Scene;
            bool hasCustomEditor = !m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].CustomEditor != nullptr;

            if (isSceneTab) {
                ImGui::MenuItem("Show Grid", nullptr, &m_ShowGrid);
                ImGui::MenuItem("Show Collisions", nullptr, &m_ShowCollisions);
                ImGui::Separator();
                if (ImGui::BeginMenu("Render Mode")) {
                    if (ImGui::MenuItem("Lit", nullptr, m_RenderMode == 0)) m_RenderMode = 0;
                    if (ImGui::MenuItem("Unlit", nullptr, m_RenderMode == 1)) m_RenderMode = 1;
                    if (ImGui::MenuItem("Wireframe", nullptr, m_RenderMode == 2)) m_RenderMode = 2;
                    ImGui::EndMenu();
                }
            } else if (hasCustomEditor) {
                m_Tabs[m_ActiveTabIndex].CustomEditor->OnImGuiMenuView();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void EditorLayer::CloseProjectInternal() {
    Project::Unload();

    m_ActiveScene = std::make_shared<Scene>();
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
}

void EditorLayer::OnUpdate(float deltaTime) {
    if (!Project::GetActive()) return;

    if (!m_TabsToClose.empty()) {
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            vkDeviceWaitIdle(VulkanRenderer::Get()->GetDevice());
        }
        
        // Sort descending so removals don't shift subsequent indices
        std::sort(m_TabsToClose.rbegin(), m_TabsToClose.rend());
        for (int index : m_TabsToClose) {
            if (index < m_Tabs.size()) {
                m_Tabs.erase(m_Tabs.begin() + index);
            }
        }
        m_TabsToClose.clear();
        
        if (m_ActiveTabIndex >= m_Tabs.size()) m_ActiveTabIndex = m_Tabs.empty() ? 0 : m_Tabs.size() - 1;
        
        // Restore scene state
        if (!m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].Type == TabType::Scene) {
            m_ActiveScene = m_Tabs[m_ActiveTabIndex].SceneContext;
            m_SceneHierarchyPanel.SetContext(m_ActiveScene);
        } else {
            m_ActiveScene = nullptr;
            m_SceneHierarchyPanel.SetContext(nullptr);
        }
    }

    ResizeViewportIfNeeded();

    // 1. LOGIQUE D'UPDATE (Indépendant de l'API Graphique)
    switch (m_SceneState) {
        case SceneState::Edit:
            UpdateEditor(deltaTime);
            break;
        case SceneState::Play:
            UpdateRuntime(deltaTime);
            break;
        case SceneState::Pause:
            UpdateEditor(deltaTime); // On permet le mouvement de caméra en pause
            break;
    }

    // =========================================================
    // --- 2. MISE A JOUR DES PANNEAUX 3D HORS-ECRAN ---
    // =========================================================
    // On boucle sur tous les onglets. S'ils ont un éditeur custom, on le met à jour !
    for (auto& tab : m_Tabs) {
        if (tab.CustomEditor) {
            tab.CustomEditor->OnUpdate(deltaTime);
        }
    }

    // =========================================================
    // --- 3. RENDU DE LA VUE SCENE PRINCIPALE ---
    // =========================================================
    m_ViewportFramebuffer->Bind();

    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        Renderer::Clear();

        glm::mat4 view = glm::lookAt(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);
        float aspect = m_ViewportSize.x / m_ViewportSize.y;
        if (std::isnan(aspect) || aspect == 0.0f) aspect = 1.0f;
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10000.0f);

        Renderer::BeginScene(view, projection, m_EditorCamera.Position);
        Renderer::RenderScene(m_ActiveScene.get(), m_RenderMode);

        if (m_ShowGrid) {
            Renderer::DrawGrid(true);
        }

        Renderer::EndScene();
    } else {
        // --- VULKAN RENDERING ---

        // 1. RENDU DES OMBRES
        // Cela va ouvrir le carnet de commandes (s'il ne l'est pas), ouvrir le RenderPass d'ombres,
        // dessiner, puis FERMER le RenderPass d'ombres.
        VulkanRenderer::Get()->PrepareShadows(m_ActiveScene.get());

        // 2. DÉMARRAGE DE LA FRAME ET CALCULS GLOBAUX
        // Maintenant qu'aucun RenderPass n'est actif, le TLAS peut se construire !
        // Puis, la fonction va ouvrir le RenderPass principal pour la suite.
        Renderer::Clear(m_ActiveScene.get());

        // 3. RENDU DE LA SCÈNE PRINCIPALE
        // Calcul de la caméra de l'éditeur
        glm::mat4 view = glm::lookAt(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);
        float aspect = m_ViewportSize.x / m_ViewportSize.y;
        if (std::isnan(aspect) || aspect == 0.0f) aspect = 1.0f;
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10000.0f);

        // Enregistre les commandes de dessin
        Renderer::BeginScene(view, projection, m_EditorCamera.Position);
        Renderer::RenderScene(m_ActiveScene.get(), m_RenderMode);

        // (On désactive la grille pour l'instant car elle utilise des shaders OpenGL)
        // if (m_ShowGrid) Renderer::DrawGrid(true);

        // Ferme le RenderPass de la scène (mais NE SOUMET PAS LA QUEUE)
        Renderer::EndScene();
    }

    m_ViewportFramebuffer->Unbind();
}

void EditorLayer::UpdateEditor(float deltaTime) {
    if (m_ViewportFocused) {
        float speed = 500.0f * deltaTime;
        if (Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT)) speed *= 3.0f;

        bool isCtrlPressed = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);

        if (!isCtrlPressed && Input::IsMouseButtonPressed(1))
        {
            if (Input::IsKeyPressed(GLFW_KEY_W)) m_EditorCamera.Position += speed * m_EditorCamera.Front;
            if (Input::IsKeyPressed(GLFW_KEY_S)) m_EditorCamera.Position -= speed * m_EditorCamera.Front;
            if (Input::IsKeyPressed(GLFW_KEY_A)) m_EditorCamera.Position -= glm::normalize(glm::cross(m_EditorCamera.Front, m_EditorCamera.WorldUp)) * speed;
            if (Input::IsKeyPressed(GLFW_KEY_D)) m_EditorCamera.Position += glm::normalize(glm::cross(m_EditorCamera.Front, m_EditorCamera.WorldUp)) * speed;
            if (Input::IsKeyPressed(GLFW_KEY_Q)) m_EditorCamera.Position -= m_EditorCamera.WorldUp * speed;
            if (Input::IsKeyPressed(GLFW_KEY_E)) m_EditorCamera.Position += m_EditorCamera.WorldUp * speed;
        }

        glm::vec2 mousePos = Input::GetMousePosition();
        glm::vec2 delta = (mousePos - m_LastMousePosition) * 0.2f;
        m_LastMousePosition = mousePos;

        // Le clic droit est la touche 1 dans GLFW
        if (Input::IsMouseButtonPressed(1)) {
            m_EditorCamera.Yaw -= delta.x;
            m_EditorCamera.Pitch -= delta.y;

            if (m_EditorCamera.Pitch > 89.0f) m_EditorCamera.Pitch = 89.0f;
            if (m_EditorCamera.Pitch < -89.0f) m_EditorCamera.Pitch = -89.0f;

            // --- LE FIX Z-UP EST ICI ---
            glm::vec3 front;
            front.x = cos(glm::radians(m_EditorCamera.Yaw)) * cos(glm::radians(m_EditorCamera.Pitch));
            front.y = sin(glm::radians(m_EditorCamera.Yaw)) * cos(glm::radians(m_EditorCamera.Pitch));
            front.z = sin(glm::radians(m_EditorCamera.Pitch)); // Z est l'axe vertical !

            m_EditorCamera.Front = glm::normalize(front);
        }
    } else {
        m_LastMousePosition = Input::GetMousePosition();
    }
}

void EditorLayer::UpdateRuntime(float deltaTime) const
{
    // --- TES VRAIES FONCTIONS DE SCENE.H ---
    m_ActiveScene->OnUpdateScripts(deltaTime);
    m_ActiveScene->OnUpdatePhysics(deltaTime);
}

void EditorLayer::ResizeViewportIfNeeded() const
{
    FramebufferSpecification spec = m_ViewportFramebuffer->GetSpecification();
    if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
        (spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
    {
        // On ne resize QUE le Framebuffer, la scène n'a pas de OnResize()
        m_ViewportFramebuffer->Resize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
    }
}

void EditorLayer::OnImGuiRender() {
    if (!Project::GetActive()) {
        m_ProjectLoadedState = false;
        m_HubPanel.OnImGuiRender();
        return;
    }

    if (!m_ProjectLoadedState) {
        m_ProjectLoadedState = true;

        // --- NOUVEAU : Chargement de la scène par défaut ---
        const std::filesystem::path startScene = Project::GetActive()->GetConfig().StartScene;

        if (const std::filesystem::path fullPath = Project::GetProjectDirectory() / startScene; !startScene.empty() && std::filesystem::exists(fullPath)) {
            OpenScene(fullPath);
        } else {
            NewScene(); // Fallback : Scène vide si aucune n'est configurée
        }
    }

    BeginDockspace();

    HandleShortcuts();

    DrawMenuBar();

    // 1. La Fenêtre qui contient la barre d'onglets (Le "Master Panel" des onglets)
    DrawTabs();

    if (m_ContentBrowserPanel) m_ContentBrowserPanel->OnImGuiRender();

    // 2. Affichage conditionnel des Layouts !
    if (!m_Tabs.empty() && m_ActiveTabIndex >= 0 && m_ActiveTabIndex < m_Tabs.size()) {
        if (const auto& activeTab = m_Tabs[m_ActiveTabIndex]; activeTab.Type == TabType::Scene) {
            // Mode Scène : On affiche l'Inspector et le Viewport
            m_SceneHierarchyPanel.OnImGuiRender();
            DrawViewportWindow();
        }
        else if (activeTab.CustomEditor) {
            // Mode Asset : On affiche uniquement les fenêtres de l'éditeur custom
            bool dummyOpen = true;
            activeTab.CustomEditor->OnImGuiRender(dummyOpen);
        }
    } else {
        // Fond par défaut si tout est fermé
        ImGui::Begin("Viewport");
        ImGui::TextDisabled("No active document open.");
        ImGui::End();
    }

    if (m_ShowProjectSettings) DrawProjectSettings();
    if (m_ShowEditorPreferences) DrawEditorPreferences();

    EndDockspace();
}

void EditorLayer::DrawTabs() {
    ImVec2 uv0 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(0, 1) : ImVec2(0, 0);
    ImVec2 uv1 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(1, 0) : ImVec2(1, 1);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Documents", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (int i = 0; i < m_Tabs.size(); i++) {
            auto& tab = m_Tabs[i];
            bool isOpen = true;

            ImGuiTabItemFlags flags = (m_ForceTabSelection && m_ActiveTabIndex == i) ? ImGuiTabItemFlags_SetSelected : 0;

            // --- 1. DÉTECTION DU TYPE D'ASSET ---
            std::string extension = ".cescene"; // Type par défaut pour une nouvelle scène vide
            if (tab.Type == TabType::Material) extension = ".cemat";
            else if (tab.Type == TabType::MaterialInstance) extension = ".cematinst";

            if (!tab.Filepath.empty()) {
                extension = tab.Filepath.extension().string();
            }

            AssetTypeInfo info;
            bool isKnownAsset = AssetRegistry::GetInfo(extension, info);

            // --- 2. APPLICATION DES COULEURS ---
            if (isKnownAsset) {
                // Couleurs basées sur l'AssetRegistry (Assombries pour ne pas agresser l'œil)
                ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(info.Color.x * 0.4f, info.Color.y * 0.4f, info.Color.z * 0.4f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(info.Color.x * 0.6f, info.Color.y * 0.6f, info.Color.z * 0.6f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(info.Color.x * 0.8f, info.Color.y * 0.8f, info.Color.z * 0.8f, 1.0f));
            }

            // On ajoute des espaces vides au nom pour décaler le texte et laisser la place à l'icône
            std::string tabTitle = "    " + tab.Name;

            bool isSelected = ImGui::BeginTabItem(tabTitle.c_str(), &isOpen, flags);

            if (isKnownAsset) {
                ImGui::PopStyleColor(3); // On n'oublie pas de dépiler les couleurs !
            }

            // --- 3. DESSIN DE L'ICÔNE (La Ruse ImGui) ---
            ImVec2 itemMin = ImGui::GetItemRectMin(); // Récupère le rectangle exact de l'onglet dessiné
            ImVec2 itemMax = ImGui::GetItemRectMax();
            float iconSize = ImGui::GetTextLineHeight();

            // On calcule la position de l'icône pour qu'elle soit centrée verticalement à gauche
            ImVec2 iconPos = ImVec2(itemMin.x + ImGui::GetStyle().FramePadding.x, itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f);

            if (isKnownAsset && info.IconID != nullptr) {
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)TextureLoader::GetImGuiTextureID(info.IconID),
                    iconPos,
                    ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                    uv0, // uv_min
                    uv1  // uv_max
                );
            }

            // --- 4. GESTION DU FOCUS ---
            if (isSelected) {
                if (m_ActiveTabIndex != i) {
                    m_ActiveTabIndex = i;
                    if (tab.Type == TabType::Scene) {
                        m_ActiveScene = tab.SceneContext;
                        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
                        // On réapplique automatiquement le mode Prefab si l'onglet est un Prefab !
                        m_SceneHierarchyPanel.SetIsPrefabScene(extension == ".ceprefab");
                    }
                }
                ImGui::EndTabItem();
            }

            // --- 5. FERMETURE ---
            if (!isOpen) {
                CloseTab(i);
            }
        }

        if (m_ForceTabSelection) m_ForceTabSelection = false;
        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorLayer::CloseTab(int index) {
    if (index < 0 || index >= m_Tabs.size()) return;
    
    // Add to deferral queue instead of destroying immediately
    if (std::find(m_TabsToClose.begin(), m_TabsToClose.end(), index) == m_TabsToClose.end()) {
        m_TabsToClose.push_back(index);
    }
}

void EditorLayer::CaptureViewportThumbnail(const std::string& projectPath) {
    // --- SÉCURITÉ : Pas encore de lecture de pixels en Vulkan ---
    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) return;

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
        if (ImGui::Selectable("Graphics", selectedCategory == 2)) selectedCategory = 2;
        ImGui::EndChild();

        ImGui::SameLine();

        // --- COLONNE DROITE : CONTENU ---
        ImGui::BeginChild("Details", ImVec2(0, 0), true);

        if (selectedCategory == 0) { // Maps & Modes
            ImGui::TextDisabled("DEFAULT MAPS");
            ImGui::Separator();
            ImGui::Spacing();

            if (const auto project = Project::GetActive()) {
                auto& config = project->GetConfig();

                ImGui::Text("Editor Startup Map");
                ImGui::SameLine(150.0f);

                char buffer[256];
                strncpy(buffer, config.StartScene.c_str(), sizeof(buffer));
                ImGui::InputText("##StartupMap", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);

                ImGui::SameLine();

                if (ImGui::Button("...##BrowseMap")) {
                    const nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };

                    if (nfdchar_t* outPath = nullptr; NFD::OpenDialog(outPath, filterItem, 1, Project::GetProjectDirectory().string().c_str()) == NFD_OKAY) {
                        const std::filesystem::path fullPath = outPath;
                        const std::filesystem::path relPath = std::filesystem::relative(fullPath, Project::GetProjectDirectory());

                        config.StartScene = relPath.string();

                        const std::string projFileName = Project::GetProjectDirectory().filename().string() + ".ceproj";
                        const std::filesystem::path projFilePath = Project::GetProjectDirectory() / projFileName;

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
            if (const auto project = Project::GetActive()) {
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
                    const std::string projFileName = Project::GetProjectDirectory().filename().string() + ".ceproj";
                    const std::filesystem::path projFilePath = Project::GetProjectDirectory() / projFileName;
                    Project::SaveActive(projFilePath);
                }
            }
        } else if (selectedCategory == 2) { // GRAPHICS
            ImGui::TextDisabled("RENDERING SETTINGS");
            ImGui::Separator();
            ImGui::Spacing();

            if (const auto project = Project::GetActive()) {
                auto& config = project->GetConfig();

                ImGui::Text("Shadow Map Resolution");
                ImGui::SameLine(200.0f);

                const char* resolutions[] = { "1024 (Low)", "2048 (Medium)", "4096 (High)", "8192 (Ultra)" };
                int currentResIndex = 1; // 2048 par défaut
                if (config.ShadowResolution == 1024) currentResIndex = 0;
                if (config.ShadowResolution == 4096) currentResIndex = 2;
                if (config.ShadowResolution == 8192) currentResIndex = 3;

                ImGui::PushItemWidth(150.0f);
                if (ImGui::Combo("##ShadowRes", &currentResIndex, resolutions, IM_ARRAYSIZE(resolutions))) {
                    uint32_t newRes = 2048;
                    if (currentResIndex == 0) newRes = 1024;
                    if (currentResIndex == 2) newRes = 4096;
                    if (currentResIndex == 3) newRes = 8192;

                    config.ShadowResolution = newRes;
                    Renderer::SetShadowResolution(newRes); // On l'applique immédiatement au GPU !

                    // Sauvegarde
                    const std::string projFileName = Project::GetProjectDirectory().filename().string() + ".ceproj";
                    Project::SaveActive(Project::GetProjectDirectory() / projFileName);
                }
                ImGui::PopItemWidth();
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorLayer::SaveScene() {
    if (m_Tabs.empty()) return;

    if (const auto& activeTab = m_Tabs[m_ActiveTabIndex]; activeTab.CustomEditor) {
        activeTab.CustomEditor->Save();
    } else if (activeTab.Type == TabType::Scene) {
        if (!activeTab.Filepath.empty()) {
            SceneSerializer serializer(activeTab.SceneContext);
            serializer.Serialize(activeTab.Filepath.string());
            std::cout << "[Editor] Saved Scene to " << activeTab.Filepath << std::endl;
        } else {
            SaveSceneAs();
        }
    }
}

void EditorLayer::SaveSceneAs() {
    if (m_Tabs.empty() || m_Tabs[m_ActiveTabIndex].Type != TabType::Scene) return;

    constexpr nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };

    if (nfdchar_t* outPath = nullptr; NFD::SaveDialog(outPath, filterItem, 1, nullptr, nullptr) == NFD_OKAY) {
        std::filesystem::path filepath = outPath;
        if (filepath.extension() != ".cescene") filepath += ".cescene";

        // On met à jour l'onglet actif
        auto& activeTab = m_Tabs[m_ActiveTabIndex];
        activeTab.Filepath = filepath;
        activeTab.Name = filepath.stem().string();

        SceneSerializer serializer(activeTab.SceneContext);
        serializer.Serialize(filepath.string());

        NFD::FreePath(outPath);
    }
}

void EditorLayer::OpenScene(const std::filesystem::path& path) {
    UndoManager::Clear();

    // Vérifie si la scène est déjà ouverte
    for (int i = 0; i < m_Tabs.size(); ++i) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i; m_ForceTabSelection = true; return;
        }
    }

    const auto newScene = std::make_shared<Scene>();
    SceneSerializer serializer(newScene);
    serializer.Deserialize(path.string());

    m_Tabs.push_back({ path.stem().string(), path, TabType::Scene, newScene, nullptr });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ForceTabSelection = true;

    m_ActiveScene = newScene;
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
}

void EditorLayer::OpenMaterial(const std::filesystem::path& path) {
    for (int i = 0; i < m_Tabs.size(); ++i) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i; m_ForceTabSelection = true; return;
        }
    }

    const auto newMatPanel = std::make_shared<MaterialEditorPanel>();
    newMatPanel->Load(path);
    newMatPanel->OnMaterialSavedCallback = [this](const std::filesystem::path& savedPath) {
        if (!m_ActiveScene) return;
        for (const auto view = m_ActiveScene->m_Registry.view<MaterialComponent>(); const auto entityID : view) {
            if (auto& mat = view.get<MaterialComponent>(entityID); mat.AssetPath == savedPath.string()) {
                // 1. On met à jour les données côté ECS (Ton code existant)
                mat.SetAndCompile(savedPath.string());

                // 2. NOUVEAU : On ordonne à Vulkan de détruire l'ancien "colis" en VRAM !
                if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                    VulkanRenderer::Get()->InvalidateEntityMaterial(entityID);
                }
            }
        }
    };

    m_Tabs.push_back({ path.stem().string(), path, TabType::Material, nullptr, newMatPanel });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ForceTabSelection = true;
}

void EditorLayer::OpenPrefab(const std::filesystem::path& path) {
    UndoManager::Clear();

    // Vérifie si le prefab est déjà ouvert dans un onglet
    for (int i = 0; i < m_Tabs.size(); ++i) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i;
            m_ForceTabSelection = true;
            return;
        }
    }

    const auto newScene = std::make_shared<Scene>();
    SceneSerializer serializer(newScene);
    serializer.Deserialize(path.string()); // On charge le prefab comme une scène

    // On crée un nouvel onglet de type "Scene", mais on indique au HierarchyPanel que c'est un Prefab
    m_Tabs.push_back({ path.stem().string(), path, TabType::Scene, newScene, nullptr });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ForceTabSelection = true;

    m_ActiveScene = newScene;
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    m_SceneHierarchyPanel.SetIsPrefabScene(true); // Indique à l'UI que c'est un Prefab
}

void EditorLayer::OpenMaterialInstance(const std::filesystem::path& path) {
    // Vérifie si l'instance est déjà ouverte
    for (int i = 0; i < m_Tabs.size(); ++i) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i;
            m_ForceTabSelection = true;
            return;
        }
    }

    const auto newMIPanel = std::make_shared<MaterialInstanceEditorPanel>();
    newMIPanel->Load(path);

    // --- LE HOT RELOAD POUR LES INSTANCES ---
    // --- LE HOT RELOAD POUR LES INSTANCES ---
    newMIPanel->OnMaterialInstanceSavedCallback = [this](const std::filesystem::path& savedPath) {
        if (!m_ActiveScene) return;

        // On parcourt TOUTES les entités de la scène qui ont un composant Matériau
        auto view = m_ActiveScene->m_Registry.view<MaterialComponent>();
        for (auto entityID : view) {
            // Si l'entité utilise l'instance qu'on vient de sauvegarder...
            if (auto& mat = view.get<MaterialComponent>(entityID); mat.AssetPath == savedPath.string()) {
                // 1. On force le rechargement depuis le disque !
                mat.SetAndCompile(savedPath.string());
                std::cout << "[Editor] Hot-Reloaded Material Instance for Entity ID: " << static_cast<uint32_t>(entityID) << std::endl;

                // 2. NOUVEAU : Invalidation Vulkan
                if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                    VulkanRenderer::Get()->InvalidateEntityMaterial(entityID);
                }
            }
        }
    };

    // On ajoute l'onglet
    m_Tabs.push_back({ path.stem().string(), path, TabType::MaterialInstance, nullptr, newMIPanel });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ForceTabSelection = true;
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

void EditorLayer::DrawSplashScreen() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    if (constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus; ImGui::Begin("SplashScreenOverlay", nullptr, flags)) {
        const ImVec2 windowSize = ImGui::GetWindowSize();

        // 1. Affichage de l'image (Centrée en haut)
        if (m_SplashTextureID) {
            constexpr float imgWidth = 544.0f;
            constexpr float imgHeight = 864.0f; // Tailles originales
            const float ratio = std::min(windowSize.x / imgWidth, (windowSize.y - 250) / imgHeight);
            const ImVec2 size(imgWidth * ratio, imgHeight * ratio);
            ImGui::SetCursorPos(ImVec2((windowSize.x - size.x) * 0.5f, 20.0f));
            ImGui::Image((ImTextureID)m_SplashTextureID, size);
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

// --- LE BOILERPLATE DU DOCKSPACE ---
void EditorLayer::BeginDockspace() {
    static bool dockspaceOpen = true;
    static bool opt_fullscreen = true;
    static bool opt_padding = false;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if (opt_fullscreen) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    if (!opt_padding) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);

    if (!opt_padding) ImGui::PopStyleVar();
    if (opt_fullscreen) ImGui::PopStyleVar(2);

    if (const ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }
}

void EditorLayer::EndDockspace() {
    ImGui::End();
}

// --- LE CŒUR : LE VIEWPORT 3D ---
void EditorLayer::DrawViewportWindow() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
    ImGui::Begin("Viewport");

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::SetWindowFocus();
    }

    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    void* textureID = m_ViewportFramebuffer->GetColorAttachmentRendererID();
    if (textureID != nullptr) {
        ImVec2 uv0 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(0, 1) : ImVec2(0, 0);
        ImVec2 uv1 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(1, 0) : ImVec2(1, 1);
        ImGui::Image((ImTextureID)textureID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, uv0, uv1);
    }

    // --- L'INCRUSTATION DE LA BARRE D'OUTILS (OVERLAY) ---
    ImGui::SetCursorPos(ImVec2(m_ViewportSize.x / 2.0f - 50.0f, 15.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.8f)); // Fond semi-transparent
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    const bool isPlaying = m_SceneState == SceneState::Play || m_SceneState == SceneState::Pause;

    if (const char* playStopIcon = isPlaying ? "STOP" : "PLAY"; ImGui::Button(playStopIcon, ImVec2(50, 30))) {
        if (m_SceneState == SceneState::Edit) OnScenePlay();
        else if (isPlaying) OnSceneStop();
    }
    ImGui::SameLine();
    if (ImGui::Button("PAUSE", ImVec2(50, 30))) {
        OnScenePause();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    // -----------------------------------------------------

    HandleViewportDragAndDrop();
    DrawGizmos();

    ImGui::End();
    ImGui::PopStyleVar();
}

// =========================================================================================
// IMPLEMENTATION DES SOUS-FONCTIONS MANQUANTES
// =========================================================================================

void EditorLayer::HandleViewportDragAndDrop() {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            if (const std::filesystem::path path = static_cast<const char*>(payload->Data); path.extension() == ".cescene") {
                OpenScene(path);
            } else if (path.extension() == ".ceprefab") {
                OpenPrefab(path);
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void EditorLayer::DrawGizmos() {
    // On ne dessine les Gizmos que si on est en mode "Edit" et qu'un objet est sélectionné
    if (Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity(); selectedEntity && m_GizmoType != -1 && m_SceneState == SceneState::Edit) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();

        // Calcule la zone exacte de la fenêtre Viewport
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

        // Projection et Vue de la caméra éditeur
        glm::mat4 cameraView = glm::lookAt(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);
        float aspect = m_ViewportSize.x / m_ViewportSize.y;
        if (std::isnan(aspect) || aspect == 0.0f) aspect = 1.0f;
        glm::mat4 cameraProjection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10000.0f);

        // Transform global de l'entité
        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        glm::mat4 transform = m_ActiveScene->GetWorldTransform(selectedEntity);

        // Système de Snapping (Aimant avec CTRL)
        bool snap = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL);
        float snapValue = 0.5f;
        if (m_GizmoType == ImGuizmo::OPERATION::ROTATE) snapValue = 45.0f;
        float snapValues[3] = { snapValue, snapValue, snapValue };

        // --- 1. VARIABLES STATIQUES POUR RETENIR L'ÉTAT ---
        static bool s_WasUsingGizmo = false;
        static TransformComponent s_StartTransform;

        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                             (ImGuizmo::OPERATION)m_GizmoType, (ImGuizmo::MODE)m_GizmoMode, glm::value_ptr(transform),
                             nullptr, snap ? snapValues : nullptr);

        // --- 2. PENDANT LE DÉPLACEMENT ---
        if (ImGuizmo::IsUsing()) {
            // Premier clic : On sauvegarde le composant complet en O(1)
            if (!s_WasUsingGizmo) {
                s_StartTransform = tc;
                s_WasUsingGizmo = true;
            }

            glm::vec3 translation, scale;
            glm::quat rotation;
            Math::DecomposeTransform(transform, translation, rotation, scale);

            tc.Location = translation;
            tc.Rotation = rotation;
            tc.Scale = scale;
        }
        // --- 3. QUAND ON RELÂCHE LE CLIC ---
        else {
            if (s_WasUsingGizmo) {
                s_WasUsingGizmo = false;

                // On enregistre la transaction proprement !
                UndoManager::BeginTransaction("Gizmo Move");
                UndoManager::PushAction(std::make_unique<EntityComponentCommand<TransformComponent>>(
                    m_ActiveScene,
                    selectedEntity.GetUUID(),
                    s_StartTransform, // L'état au moment du clic
                    tc                // L'état final au moment du relâchement
                ));
                UndoManager::EndTransaction();
            }
        }
    }
}

// =========================================================================================
// GESTION DES RACCOURCIS ET ACTIONS GLOBALES
// =========================================================================================

void EditorLayer::NewScene() {
    UndoManager::Clear();

    const auto newScene = std::make_shared<Scene>();
    m_Tabs.push_back({ "New Scene", "", TabType::Scene, newScene, nullptr });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ForceTabSelection = true;

    m_ActiveScene = newScene;
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
}

void EditorLayer::HandleShortcuts() {
    if (ImGui::GetIO().WantTextInput) return;

    bool ctrl = ImGui::GetIO().KeyCtrl;
    bool shift = ImGui::GetIO().KeyShift;

    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        UndoManager::Undo();
    }
    else if ((ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
             (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Y, false))) {
        UndoManager::Redo();
             }

    // --- Sauvegarde (Ctrl + S) ---
    if (const bool shift = ImGui::GetIO().KeyShift; ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (!m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].CustomEditor != nullptr) m_Tabs[m_ActiveTabIndex].CustomEditor->Save();
        else SaveScene();
    }
    // --- Sauvegarder sous (Ctrl + Shift + S) ---
    else if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        SaveSceneAs();
    }
    // --- Nouvelle Scène (Ctrl + N) ---
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        NewScene();
    }
    // --- Ouvrir Scène (Ctrl + O) ---
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        constexpr nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };
        if (nfdchar_t* outPath = nullptr; NFD::OpenDialog(outPath, filterItem, 1, nullptr) == NFD_OKAY) {
            OpenScene(outPath);
            NFD::FreePath(outPath);
        }
    }
}

Entity Scene::GetEntityByUUID(UUID uuid) {
    auto view = m_Registry.view<IDComponent>();
    for (auto entity : view) {
        auto& idComp = view.get<IDComponent>(entity);
        if (idComp.ID == uuid) {
            return { entity, this };
        }
    }
    return {}; // Retourne une entité nulle si non trouvée
}

// =========================================================================================
// EDITOR PREFERENCES
// =========================================================================================

void EditorLayer::LoadEditorPreferences() {
    // 1. On récupère la scale de l'OS (Ton 145% Linux -> 1.45f)
    float osScaleX, osScaleY;
    glfwGetWindowContentScale(Application::Get().GetWindow(), &osScaleX, &osScaleY);
    m_UIScale = osScaleX; // Par défaut, on épouse le système !

    // 2. On écrase avec le choix de l'utilisateur si le fichier existe
    std::ifstream file("editor_preferences.json");
    if (file.is_open()) {
        nlohmann::json data;
        try {
            file >> data;
            if (data.contains("UIScale")) m_UIScale = data["UIScale"].get<float>();
        } catch(...) {}
        file.close();
    }

    // 3. Application
    ImGui::GetIO().FontGlobalScale = m_UIScale;
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    UITheme::Apply();
    style.ScaleAllSizes(m_UIScale);
}

void EditorLayer::SaveEditorPreferences() {
    nlohmann::json data;

    // On charge le fichier existant pour ne pas écraser d'autres paramètres futurs
    std::ifstream inFile("editor_preferences.json");
    if (inFile.is_open()) {
        try { inFile >> data; } catch(...) {}
        inFile.close();
    }

    data["UIScale"] = m_UIScale;

    // --- SAUVEGARDE DE LA TAILLE DE LA FENÊTRE ---
    GLFWwindow* window = Application::Get().GetWindow();
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);

    // Règle d'or : On ne sauvegarde la largeur/hauteur QUE si la fenêtre n'est pas maximisée.
    // Sinon, quand on quittera le mode maximisé plus tard, la fenêtre prendra la taille de l'écran entier.
    if (!maximized) {
        data["WindowWidth"] = w;
        data["WindowHeight"] = h;
    }
    data["WindowMaximized"] = maximized;

    // Écriture sur le disque
    std::ofstream outFile("editor_preferences.json");
    if (outFile.is_open()) {
        outFile << data.dump(4);
        outFile.close();
    }
}

void EditorLayer::DrawEditorPreferences() {
    if (ImGui::Begin("Editor Preferences", &m_ShowEditorPreferences)) {
        ImGui::TextDisabled("APPEARANCE");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("UI Scale");
        ImGui::SameLine(150);
        ImGui::PushItemWidth(-1);

        // On intercepte la modification du slider pour voir le rendu en direct !
        if (ImGui::SliderFloat("##UIScale", &m_UIScale, 0.5f, 3.0f, "%.2fx")) {
            ImGui::GetIO().FontGlobalScale = m_UIScale;

            ImGuiStyle& style = ImGui::GetStyle();
            style = ImGuiStyle(); // On réinitialise à l'état pur
            UITheme::Apply();     // On réapplique nos couleurs et nos coins arrondis
            style.ScaleAllSizes(m_UIScale); // On multiplie les dimensions (padding, etc.)
        }
        ImGui::PopItemWidth();

        // Sauvegarde automatique quand on lâche le clic de la souris
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            SaveEditorPreferences();
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset to Default", ImVec2(150 * m_UIScale, 0))) {
            float osScaleX, osScaleY;
            glfwGetWindowContentScale(Application::Get().GetWindow(), &osScaleX, &osScaleY);

            m_UIScale = osScaleX; // Retour au 145% naturel !
            ImGui::GetIO().FontGlobalScale = m_UIScale;

            ImGuiStyle& style = ImGui::GetStyle();
            style = ImGuiStyle();
            UITheme::Apply();
            style.ScaleAllSizes(m_UIScale);

            SaveEditorPreferences();
        }
    }
    ImGui::End();
}