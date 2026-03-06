#include "MaterialInstanceEditorPanel.h"
#include <imgui.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h> // Pour le temps (Rotation de la sphère)

#include "project/Project.h"
#include "renderer/Renderer.h"
#include "renderer/PrimitiveFactory.h"
#include "renderer/TextureLoader.h"

void MaterialInstanceEditorPanel::Load(const std::filesystem::path& path) {
    m_CurrentPath = path;
    
    // 1. Initialiser la preview 3D
    if (!m_PreviewFramebuffer) {
        FramebufferSpecification spec;
        spec.Width = 800; spec.Height = 600;
        m_PreviewFramebuffer = std::make_shared<Framebuffer>(spec);
        // On assigne directement le mesh retourné par la factory !
        m_PreviewMesh = PrimitiveFactory::CreateSphere();
    }

    // 2. Charger le JSON de l'instance
    std::ifstream file(path);
    if (file.is_open()) {
        nlohmann::json data = nlohmann::json::parse(file);
        if (data.contains("Parent")) {
            m_ParentMaterialPath = data["Parent"].get<std::string>();
            LoadParentParameters(); // On charge les paramètres par défaut du parent
        }

        // 3. Appliquer les overrides de l'instance
        if (data.contains("Overrides")) {
            auto overrides = data["Overrides"];
            for (auto& [key, param] : m_Parameters) {
                if (overrides.contains(key)) {
                    param.IsOverridden = true;
                    if (param.Type == "Float") param.FloatVal = overrides[key].get<float>();
                    else if (param.Type == "Color") {
                        auto col = overrides[key];
                        param.ColorVal = {col[0], col[1], col[2], col[3]};
                    }
                    else if (param.Type == "Texture2D") {
                        param.TexturePath = overrides[key].get<std::string>();
                        if (!param.TexturePath.empty()) {
                            std::filesystem::path fullTex = Project::GetProjectDirectory() / param.TexturePath;
                            param.TextureID = TextureLoader::LoadTexture(fullTex.string().c_str());
                        }
                    }
                }
            }
        }
        file.close();
        CompilePreviewShader();
    }
}

void MaterialInstanceEditorPanel::OpenAsset(const std::filesystem::path& path) {
    Load(path);
}

void MaterialInstanceEditorPanel::LoadParentParameters() {
    m_Parameters.clear();
    if (m_ParentMaterialPath.empty()) return;

    std::filesystem::path fullParentPath = Project::GetProjectDirectory() / m_ParentMaterialPath;
    std::ifstream file(fullParentPath);
    if (file.is_open()) {
        nlohmann::json data = nlohmann::json::parse(file);
        if (data.contains("Nodes")) {
            for (auto& nodeJson : data["Nodes"]) {
                if (nodeJson.contains("IsParameter") && nodeJson["IsParameter"].get<bool>()) {
                    MIParameter param;
                    param.Name = nodeJson["ParameterName"].get<std::string>();
                    param.Type = nodeJson["Name"].get<std::string>(); // "Float", "Color", "Texture2D"
                    
                    if (param.Type == "Float") param.FloatVal = nodeJson["FloatValue"].get<float>();
                    else if (param.Type == "Color") {
                        auto col = nodeJson["ColorValue"];
                        param.ColorVal = {col[0], col[1], col[2], col[3]};
                    }
                    else if (param.Type == "Texture2D") {
                        param.TexturePath = nodeJson["TexturePath"].get<std::string>();
                        if (!param.TexturePath.empty()) {
                            std::filesystem::path fullTex = Project::GetProjectDirectory() / param.TexturePath;
                            param.TextureID = TextureLoader::LoadTexture(fullTex.string().c_str());
                        }
                    }
                    m_Parameters[param.Name] = param;
                }
            }
        }
        file.close();
    }
}

void MaterialInstanceEditorPanel::CompilePreviewShader() {
    if (m_ParentMaterialPath.empty()) return;
    
    // Le génie de l'instance : on récupère juste le code GLSL généré par le parent depuis le cache !
    std::filesystem::path parentPath = std::filesystem::path(m_ParentMaterialPath);
    std::filesystem::path cacheDir = Project::GetCacheDirectory();
    std::filesystem::path fragPath = cacheDir / (parentPath.stem().string() + ".frag");
    
    if (std::filesystem::exists(fragPath)) {
        m_PreviewShader = std::make_shared<Shader>("shaders/default.vert", fragPath.string().c_str());
    }
}

void MaterialInstanceEditorPanel::OnImGuiRender(bool& isOpen) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Material Instance Editor", &isOpen);
    ImGui::PopStyleVar();

    ImGui::Columns(2, "MI_Columns");
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.65f); // 65% pour la 3D

    // ==========================================
    // COLONNE GAUCHE : PREVIEW 3D
    // ==========================================
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (m_PreviewFramebuffer && viewportSize.x > 0 && viewportSize.y > 0) {
        m_PreviewFramebuffer->Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        m_PreviewFramebuffer->Bind();
        glViewport(0, 0, (int)viewportSize.x, (int)viewportSize.y);

        // --- FIX : On active la profondeur et on nettoie comme dans l'éditeur parent ---
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (m_PreviewShader && m_PreviewMesh) {
            m_PreviewShader->Use();

            // --- FIX : Matrices exactes attendues par le Vertex Shader par défaut ---
            float aspect = viewportSize.x / viewportSize.y;
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 10.0f, 10000.0f);
            glm::vec3 camPos = glm::vec3(0.0f, 0.0f, m_CameraDistance);
            glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

            m_PreviewShader->SetMat4("uProjection", proj);
            m_PreviewShader->SetMat4("uView", view);

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::rotate(model, glm::radians(m_RotationSpeed * (float)glfwGetTime()), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.8f));
            m_PreviewShader->SetMat4("uModel", model);

            // --- FIX : Éclairage PBR indispensable pour ne pas être noir ---
            m_PreviewShader->SetVec3("uLightPos", glm::vec3(1.0f, 1.0f, 1.0f));
            m_PreviewShader->SetVec3("uLightColor", glm::vec3(3.0f, 3.0f, 3.0f));
            m_PreviewShader->SetVec3("uViewPos", camPos);
            m_PreviewShader->SetFloat("uTime", (float)glfwGetTime());

            // --- Envoi des paramètres modifiés (Uniforms) ---
            int texSlot = 0;
            for (auto& [key, param] : m_Parameters) {
                if (param.Type == "Float") m_PreviewShader->SetFloat("u_" + key, param.FloatVal);
                else if (param.Type == "Color") m_PreviewShader->SetVec3("u_" + key, param.ColorVal);
                else if (param.Type == "Texture2D" && param.TextureID != 0) {
                    glActiveTexture(GL_TEXTURE0 + texSlot);
                    glBindTexture(GL_TEXTURE_2D, param.TextureID);
                    m_PreviewShader->SetInt("u_" + key, texSlot);
                    texSlot++;
                }
            }

            m_PreviewMesh->Draw();

            // Nettoyage des slots de texture
            for (int i = 0; i < 8; i++) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glActiveTexture(GL_TEXTURE0);
        }
        
        m_PreviewFramebuffer->Unbind();
        ImGui::Image((ImTextureID)(uintptr_t)m_PreviewFramebuffer->GetColorAttachmentRendererID(), viewportSize, ImVec2(0, 1), ImVec2(1, 0));
    }
    
    // ==========================================
    // COLONNE DROITE : DETAILS ET OVERRIDES
    // ==========================================
    ImGui::NextColumn();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::BeginChild("DetailsPanel");

    ImGui::TextDisabled("GENERAL");
    ImGui::Separator();
    
    char parentBuf[256];
    strncpy(parentBuf, m_ParentMaterialPath.c_str(), sizeof(parentBuf));
    ImGui::InputText("Parent Material", parentBuf, sizeof(parentBuf), ImGuiInputTextFlags_ReadOnly);
    
    // Drag & Drop pour assigner le parent
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::string path = (const char*)payload->Data;
            std::filesystem::path fp = path;
            if (fp.extension() == ".cemat") {
                m_ParentMaterialPath = std::filesystem::relative(fp, Project::GetProjectDirectory()).string();
                LoadParentParameters();
                CompilePreviewShader();
            }
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::TextDisabled("PARAMETERS");
    ImGui::Separator();
    
    for (auto& [key, param] : m_Parameters) {
        ImGui::PushID(key.c_str());
        
        // La case à cocher Unreal Engine Style !
        if (ImGui::Checkbox("##override", &param.IsOverridden)) {
            if (!param.IsOverridden) LoadParentParameters(); // Reset à la valeur par défaut
        }
        ImGui::SameLine();
        ImGui::Text("%s", key.c_str());
        
        if (!param.IsOverridden) ImGui::BeginDisabled();
        
        ImGui::PushItemWidth(-1);
        if (param.Type == "Float") {
            ImGui::DragFloat("##f", &param.FloatVal, 0.01f);
        } else if (param.Type == "Color") {
            ImGui::ColorEdit4("##c", glm::value_ptr(param.ColorVal));
        } else if (param.Type == "Texture2D") {
            std::string btnText = param.TexturePath.empty() ? "Drop Texture Here" : std::filesystem::path(param.TexturePath).filename().string();
            ImGui::Button(btnText.c_str(), ImVec2(-1, 24)); // Bouton qui prend toute la largeur

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::filesystem::path fp = (const char*)payload->Data;
                    if (fp.extension() == ".png" || fp.extension() == ".jpg") {
                        param.TexturePath = std::filesystem::relative(fp, Project::GetProjectDirectory()).string();
                        param.TextureID = TextureLoader::LoadTexture(fp.string().c_str());
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::PopItemWidth();
        
        if (!param.IsOverridden) ImGui::EndDisabled();
        
        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::End();
}

void MaterialInstanceEditorPanel::Save() {
    nlohmann::json data;
    data["Parent"] = m_ParentMaterialPath;
    
    nlohmann::json overrides;
    for (auto& [key, param] : m_Parameters) {
        if (param.IsOverridden) {
            if (param.Type == "Float") overrides[key] = param.FloatVal;
            else if (param.Type == "Color") overrides[key] = {param.ColorVal.x, param.ColorVal.y, param.ColorVal.z, param.ColorVal.w};
            else if (param.Type == "Texture2D") overrides[key] = param.TexturePath;
        }
    }
    data["Overrides"] = overrides;
    
    std::ofstream file(m_CurrentPath);
    if (file.is_open()) {
        file << data.dump(4);
        file.close();
        std::cout << "[Editor] Saved Material Instance: " << m_CurrentPath.filename() << std::endl;
        
        if (OnMaterialInstanceSavedCallback) {
            OnMaterialInstanceSavedCallback(m_CurrentPath);
        }
    }
}