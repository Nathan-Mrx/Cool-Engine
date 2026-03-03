#include "EditorLayer.h"
#include <imgui.h>
#include <glad/glad.h>

#include "core/Application.h"
#include "core/Input.h"
#include "ecs/Components.h"
#include "project/Project.h"
#include "renderer/Renderer.h"
#include "scene/SceneSerializer.h"

void EditorLayer::OnAttach() {
    m_ActiveScene = std::make_shared<Scene>();
    // Utilisation du nom correct défini dans le header
    m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    m_ContentBrowserPanel = std::make_unique<ContentBrowserPanel>();

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
            if (ImGui::MenuItem("Exit")) Application::Get().Close();
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

    ImGui::DockSpace(ImGui::GetID("MyEngineDockSpace"), ImVec2(0.0f, 0.0f), dockspace_flags);
}

void EditorLayer::EndDockspace() {
    ImGui::End();
}

void EditorLayer::OnUpdate(float ts) {
    if (!m_ActiveScene || !m_ViewportFramebuffer) return;

    if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f) return;
    m_ViewportFramebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

    // --- GESTION DE LA CAMÉRA ---
    GLFWwindow* window = Application::Get().GetWindow();
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

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

    m_ViewportFramebuffer->Bind();
    glViewport(0, 0, (GLsizei)m_ViewportSize.x, (GLsizei)m_ViewportSize.y);
    Renderer::Clear();

    // 4. Calcul des matrices (S'assurer de ne les déclarer qu'UNE SEULE fois)
    float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
    glm::mat4 projection = glm::perspectiveLH(glm::radians(45.0f), aspectRatio, 0.1f, 100000.0f);
    glm::mat4 view = glm::lookAtLH(m_EditorCamera.Position, m_EditorCamera.Position + m_EditorCamera.Front, m_EditorCamera.WorldUp);

    Renderer::BeginScene(view, projection, m_EditorCamera.Position);
    Renderer::DrawGrid(m_ShowGrid);
    Renderer::RenderScene(m_ActiveScene.get());
    Renderer::EndScene();

    m_ViewportFramebuffer->Unbind();
}

void EditorLayer::OnImGuiRender() {
    BeginDockspace();
    DrawMenuBar();

    if (Project::GetActive() == nullptr) {
        m_HubPanel.OnImGuiRender();
    } else
    {
        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel->OnImGuiRender();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 }); // Enlève les bordures moches
        ImGui::Begin("Viewport");

        // 1. On capture la taille EXACTE du panneau une fois qu'il est ouvert
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

        // 2. On affiche la texture
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

        ImGui::End();
        ImGui::PopStyleVar();
    }
    EndDockspace();
}