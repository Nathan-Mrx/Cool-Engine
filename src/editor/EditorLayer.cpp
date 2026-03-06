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


void EditorLayer::OnAttach() {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load("splash.png", &width, &height, &channels, 4);
    if (data) {
        glGenTextures(1, &m_SplashTextureID);
        glBindTexture(GL_TEXTURE_2D, m_SplashTextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }

    AssetRegistry::RegisterAllAssets();

    // 1. Initialisation pure de la Scène
    m_ActiveScene = std::make_shared<Scene>();
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();

    // 2. Callbacks du Content Browser
    m_ContentBrowserPanel->OnSceneOpenCallback = [this](const std::filesystem::path& path) { OpenScene(path); };
    m_ContentBrowserPanel->OnPrefabOpenCallback = [this](const std::filesystem::path& path) { OpenPrefab(path); };
    m_ContentBrowserPanel->OnMaterialOpenCallback = [this](const std::filesystem::path& path) { OpenMaterial(path); };
    m_ContentBrowserPanel->OnMaterialInstanceOpenCallback = [this](const std::filesystem::path& path) { OpenMaterialInstance(path); };

    FramebufferSpecification fbSpec;
    fbSpec.Width = 1280;
    fbSpec.Height = 720;
    m_ViewportFramebuffer = std::make_unique<Framebuffer>(fbSpec);
}

void EditorLayer::OnDetach() {
    // Nettoyage si nécessaire
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
            bool hasCustomEditor = !m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].CustomEditor != nullptr;

            if (hasCustomEditor) {
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
            if (ImGui::MenuItem("Project Settings")) m_ShowProjectSettings = true;

            bool hasCustomEditor = !m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].CustomEditor != nullptr;
            if (hasCustomEditor) {
                ImGui::Separator();
                m_Tabs[m_ActiveTabIndex].CustomEditor->OnImGuiMenuEdit();
            }
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

    ResizeViewportIfNeeded();

    // Rendu hors-écran dans le Framebuffer
    m_ViewportFramebuffer->Bind();

    // --- LE VRAI APPEL DE TON RENDERER ---
    Renderer::Clear();

    // 1. LOGIQUE D'UPDATE
    switch (m_SceneState) {
        case SceneState::Edit:
            UpdateEditor(deltaTime);
            break;
        case SceneState::Play:
            UpdateRuntime(deltaTime);
            break;
        case SceneState::Pause:
            // En pause, on met quand même à jour la caméra pour pouvoir bouger
            UpdateEditor(deltaTime);
            break;
    }

    // 2. PREPARATION DES MATRICES
    // Calcul de la vue et projection depuis l'EditorCamera
    glm::mat4 view = glm::lookAt(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);
    float aspect = m_ViewportSize.x / m_ViewportSize.y;
    if (std::isnan(aspect) || aspect == 0.0f) aspect = 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10000.0f);

    // 3. RENDU
    // --- TES VRAIES FONCTIONS DE RENDERER.H ---
    Renderer::BeginScene(view, projection, m_EditorCamera.Position);

    Renderer::RenderScene(m_ActiveScene.get(), m_RenderMode);

    if (m_ShowGrid) {
        Renderer::DrawGrid(true);
    }

    Renderer::EndScene();

    m_ViewportFramebuffer->Unbind();
}

void EditorLayer::UpdateEditor(float deltaTime) {
    if (m_ViewportFocused) {
        float speed = 1000.0f * deltaTime;
        if (Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT)) speed *= 3.0f;

        if (Input::IsKeyPressed(GLFW_KEY_W)) m_EditorCamera.Position += speed * m_EditorCamera.Front;
        if (Input::IsKeyPressed(GLFW_KEY_S)) m_EditorCamera.Position -= speed * m_EditorCamera.Front;
        if (Input::IsKeyPressed(GLFW_KEY_A)) m_EditorCamera.Position -= glm::normalize(glm::cross(m_EditorCamera.Front, m_EditorCamera.WorldUp)) * speed;
        if (Input::IsKeyPressed(GLFW_KEY_D)) m_EditorCamera.Position += glm::normalize(glm::cross(m_EditorCamera.Front, m_EditorCamera.WorldUp)) * speed;
        if (Input::IsKeyPressed(GLFW_KEY_Q)) m_EditorCamera.Position -= glm::normalize(glm::cross(m_EditorCamera.WorldUp, m_EditorCamera.Front)) * speed;
        if (Input::IsKeyPressed(GLFW_KEY_E)) m_EditorCamera.Position += glm::normalize(glm::cross(m_EditorCamera.WorldUp, m_EditorCamera.Front)) * speed;

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

void EditorLayer::UpdateRuntime(float deltaTime) {
    // --- TES VRAIES FONCTIONS DE SCENE.H ---
    m_ActiveScene->OnUpdateScripts(deltaTime);
    m_ActiveScene->OnUpdatePhysics(deltaTime);
}

void EditorLayer::ResizeViewportIfNeeded() {
    FramebufferSpecification spec = m_ViewportFramebuffer->GetSpecification();
    if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
        (spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
    {
        // On ne resize QUE le Framebuffer, la scène n'a pas de OnResize()
        m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
    }
}

void EditorLayer::OnImGuiRender() {
    if (!Project::GetActive()) {
        m_HubPanel.OnImGuiRender();
        return;
    }

    BeginDockspace();
    DrawMenuBar();

    // 1. La Fenêtre qui contient la barre d'onglets (Le "Master Panel" des onglets)
    DrawTabs();

    if (m_ContentBrowserPanel) m_ContentBrowserPanel->OnImGuiRender();

    // 2. Affichage conditionnel des Layouts !
    if (!m_Tabs.empty() && m_ActiveTabIndex >= 0 && m_ActiveTabIndex < m_Tabs.size()) {
        auto& activeTab = m_Tabs[m_ActiveTabIndex];

        if (activeTab.Type == TabType::Scene) {
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

    EndDockspace();
}

void EditorLayer::DrawTabs() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    // Une fenêtre dédiée juste pour la barre de navigation !
    ImGui::Begin("Documents", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (int i = 0; i < m_Tabs.size(); i++) {
            auto& tab = m_Tabs[i];
            bool isOpen = true;

            ImGuiTabItemFlags flags = (m_ForceTabSelection && m_ActiveTabIndex == i) ? ImGuiTabItemFlags_SetSelected : 0;

            if (ImGui::BeginTabItem(tab.Name.c_str(), &isOpen, flags)) {
                // Changement de contexte automatique
                if (m_ActiveTabIndex != i) {
                    m_ActiveTabIndex = i;
                    if (tab.Type == TabType::Scene) {
                        m_ActiveScene = tab.SceneContext;
                        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
                    }
                }
                ImGui::EndTabItem();
            }

            if (!isOpen) {
                CloseTab(i);
                i--; // L'onglet a été supprimé, on recule l'index
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
    m_Tabs.erase(m_Tabs.begin() + index);

    if (m_ActiveTabIndex >= m_Tabs.size()) m_ActiveTabIndex = m_Tabs.size() - 1;

    // Restaure la scène si le nouvel onglet actif est une scène
    if (!m_Tabs.empty() && m_Tabs[m_ActiveTabIndex].Type == TabType::Scene) {
        m_ActiveScene = m_Tabs[m_ActiveTabIndex].SceneContext;
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    } else {
        m_ActiveScene = nullptr; // Désactive la scène en arrière plan
    }
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
    if (m_Tabs.empty()) return;
    auto& activeTab = m_Tabs[m_ActiveTabIndex];

    if (activeTab.CustomEditor) {
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

    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItem[1] = { { "Cool Engine Scene", "cescene" } };

    if (NFD::SaveDialog(outPath, filterItem, 1, nullptr, nullptr) == NFD_OKAY) {
        std::filesystem::path filepath = outPath;
        if (filepath.extension() != ".cescene") filepath += ".cescene";

        // On met à jour l'onglet actif
        auto& activeTab = m_Tabs[m_ActiveTabIndex];
        activeTab.Filepath = filepath;
        activeTab.Name = filepath.filename().string();

        SceneSerializer serializer(activeTab.SceneContext);
        serializer.Serialize(filepath.string());

        NFD::FreePath(outPath);
    }
}

void EditorLayer::OpenScene(const std::filesystem::path& path) {
    // Vérifie si la scène est déjà ouverte
    for (int i = 0; i < m_Tabs.size(); ++i) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i; m_ForceTabSelection = true; return;
        }
    }

    auto newScene = std::make_shared<Scene>();
    SceneSerializer serializer(newScene);
    serializer.Deserialize(path.string());

    m_Tabs.push_back({ path.filename().string(), path, TabType::Scene, newScene, nullptr });
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

    auto newMatPanel = std::make_shared<MaterialEditorPanel>();
    newMatPanel->Load(path);
    newMatPanel->OnMaterialSavedCallback = [this](const std::filesystem::path& savedPath) {
        if (!m_ActiveScene) return;
        auto view = m_ActiveScene->m_Registry.view<MaterialComponent>();
        for (auto entityID : view) {
            auto& mat = view.get<MaterialComponent>(entityID);
            if (mat.AssetPath == savedPath.string()) mat.SetAndCompile(savedPath.string());
        }
    };

    m_Tabs.push_back({ path.filename().string(), path, TabType::Material, nullptr, newMatPanel });
    m_ActiveTabIndex = m_Tabs.size() - 1;
    m_ForceTabSelection = true;
}

void EditorLayer::OpenPrefab(const std::filesystem::path& path) {
    // Vérifie si le prefab est déjà ouvert dans un onglet
    for (int i = 0; i < m_Tabs.size(); ++i) {
        if (m_Tabs[i].Filepath == path) {
            m_ActiveTabIndex = i;
            m_ForceTabSelection = true;
            return;
        }
    }

    auto newScene = std::make_shared<Scene>();
    SceneSerializer serializer(newScene);
    serializer.Deserialize(path.string()); // On charge le prefab comme une scène

    // On crée un nouvel onglet de type "Scene", mais on indique au HierarchyPanel que c'est un Prefab
    m_Tabs.push_back({ path.filename().string(), path, TabType::Scene, newScene, nullptr });
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

    auto newMIPanel = std::make_shared<MaterialInstanceEditorPanel>();
    newMIPanel->Load(path);

    // --- LE HOT RELOAD POUR LES INSTANCES ---
    newMIPanel->OnMaterialInstanceSavedCallback = [this](const std::filesystem::path& savedPath) {
        if (!m_ActiveScene) return;

        // On parcourt TOUTES les entités de la scène qui ont un composant Matériau
        auto view = m_ActiveScene->m_Registry.view<MaterialComponent>();
        for (auto entityID : view) {
            auto& mat = view.get<MaterialComponent>(entityID);

            // Si l'entité utilise l'instance qu'on vient de sauvegarder...
            if (mat.AssetPath == savedPath.string()) {
                // On force le rechargement depuis le disque !
                mat.SetAndCompile(savedPath.string());
                std::cout << "[Editor] Hot-Reloaded Material Instance for Entity ID: " << (uint32_t)entityID << std::endl;
            }
        }
    };

    // On ajoute l'onglet
    m_Tabs.push_back({ path.filename().string(), path, TabType::MaterialInstance, nullptr, newMIPanel });
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

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
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

    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    uint32_t textureID = m_ViewportFramebuffer->GetColorAttachmentRendererID();
    ImGui::Image((ImTextureID)(uintptr_t)textureID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

    // --- L'INCRUSTATION DE LA BARRE D'OUTILS (OVERLAY) ---
    ImGui::SetCursorPos(ImVec2(m_ViewportSize.x / 2.0f - 50.0f, 15.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.8f)); // Fond semi-transparent
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    bool isPlaying = m_SceneState == SceneState::Play || m_SceneState == SceneState::Pause;
    const char* playStopIcon = isPlaying ? "STOP" : "PLAY";

    if (ImGui::Button(playStopIcon, ImVec2(50, 30))) {
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
            std::filesystem::path path = (const char*)payload->Data;

            if (path.extension() == ".cescene") {
                OpenScene(path);
            } else if (path.extension() == ".ceprefab") {
                OpenPrefab(path);
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void EditorLayer::DrawGizmos() {
    Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

    // On ne dessine les Gizmos que si on est en mode "Edit" et qu'un objet est sélectionné
    if (selectedEntity && m_GizmoType != -1 && m_SceneState == SceneState::Edit) {
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

        // Dessin du Gizmo et récupération des manipulations
        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                             (ImGuizmo::OPERATION)m_GizmoType, (ImGuizmo::MODE)m_GizmoMode, glm::value_ptr(transform),
                             nullptr, snap ? snapValues : nullptr);

        // Si l'utilisateur est en train de tirer sur la flèche du Gizmo
        if (ImGuizmo::IsUsing()) {
            glm::vec3 translation, scale;
            glm::quat rotation;

            // On décompose la matrice de manipulation
            Math::DecomposeTransform(transform, translation, rotation, scale);

            // On assigne directement sans passer par un "delta"
            tc.Location = translation;
            tc.Rotation = rotation;
            tc.Scale = scale;
        }
    }
}