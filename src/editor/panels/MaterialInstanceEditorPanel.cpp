#include "MaterialInstanceEditorPanel.h"
#include <imgui.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>

#include "project/Project.h"
#include "renderer/Renderer.h"
#include "renderer/PrimitiveFactory.h"
#include "renderer/TextureLoader.h"

#include "editor/EditorCommands.h"
#include "editor/UndoManager.h"

using MIState = std::pair<std::string, std::unordered_map<std::string, MIParameter>>;
// =========================================================================================
// COMMANDE D'UNDO POUR LES INSTANCES DE MATERIAUX (Intelligente)
// =========================================================================================
class MaterialInstanceCommand : public IUndoableAction {
public:
    MaterialInstanceCommand(MaterialInstanceEditorPanel* panel, std::filesystem::path assetPath, MIState oldState, MIState newState)
        : m_Panel(panel), m_AssetPath(std::move(assetPath)), m_OldState(std::move(oldState)), m_NewState(std::move(newState)) {}

    void ApplyState(const MIState& state) {
        // 1. Si le panel est ouvert sur le BON asset, on met à jour l'UI en direct
        if (m_Panel && m_Panel->GetCurrentPath() == m_AssetPath) {
            m_Panel->SetFullState(state.first, state.second);
        } else {
            // 2. Sinon, on sauvegarde en arrière-plan directement dans le fichier !
            nlohmann::json data;
            data["Parent"] = state.first;
            nlohmann::json overrides;
            for (auto& [key, param] : state.second) {
                if (param.IsOverridden) {
                    if (param.Type == "Float") overrides[key] = param.FloatVal;
                    else if (param.Type == "Color") overrides[key] = {param.ColorVal.x, param.ColorVal.y, param.ColorVal.z, param.ColorVal.w};
                    else if (param.Type == "Texture2D") overrides[key] = param.TexturePath;
                    else if (param.Type == "StaticSwitchParameter") overrides[key] = param.BoolVal;
                }
            }
            data["Overrides"] = overrides;
            std::ofstream file(m_AssetPath);
            if (file.is_open()) file << data.dump(4);
        }
    }

    void Undo() override { ApplyState(m_OldState); }
    void Redo() override { ApplyState(m_NewState); }

private:
    MaterialInstanceEditorPanel* m_Panel;
    std::filesystem::path m_AssetPath;
    MIState m_OldState;
    MIState m_NewState;
};


void MaterialInstanceEditorPanel::Load(const std::filesystem::path& path) {
    m_CurrentPath = path;
    if (!m_PreviewFramebuffer) {
        FramebufferSpecification spec;
        spec.Width = 800; spec.Height = 600;
        m_PreviewFramebuffer = Framebuffer::Create(spec);

        // --- SÉCURITÉ : Pas de création de Mesh OpenGL en Vulkan ! ---
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            m_PreviewMesh = PrimitiveFactory::CreateSphere();
        }
    }

    std::ifstream file(path);
    if (file.is_open()) {
        nlohmann::json data = nlohmann::json::parse(file);
        if (data.contains("Parent")) {
            m_ParentMaterialPath = data["Parent"].get<std::string>();
            LoadParentParameters();
        }

        if (data.contains("Overrides")) {
            auto overrides = data["Overrides"];
            for (auto& [key, param] : m_Parameters) {
                if (overrides.contains(key)) {
                    if (param.Type == "Float" && overrides[key].is_number()) { param.IsOverridden = true; param.FloatVal = overrides[key].get<float>(); }
                    else if (param.Type == "Color" && overrides[key].is_array() && overrides[key].size() >= 4) {
                        param.IsOverridden = true; auto col = overrides[key]; param.ColorVal = {col[0], col[1], col[2], col[3]};
                    }
                    else if (param.Type == "Texture2D" && overrides[key].is_string()) {
                        param.IsOverridden = true; param.TexturePath = overrides[key].get<std::string>();
                        if (!param.TexturePath.empty()) param.TextureID = TextureLoader::LoadTexture((Project::GetProjectDirectory() / param.TexturePath).string().c_str());
                    }
                    else if (param.Type == "StaticSwitchParameter" && overrides[key].is_boolean()) {
                        param.IsOverridden = true; param.BoolVal = overrides[key].get<bool>();
                    }
                }
            }
        }
        file.close();
        CompilePreviewShader();
        EvaluateParameterVisibility();
    }
}

void MaterialInstanceEditorPanel::OpenAsset(const std::filesystem::path& path) { Load(path); }

void MaterialInstanceEditorPanel::LoadParentParameters() {
    m_Parameters.clear(); m_StaticTextures.clear();
    if (m_ParentMaterialPath.empty()) return;

    std::ifstream file(Project::GetProjectDirectory() / m_ParentMaterialPath);
    if (file.is_open()) {
        m_ParentGraphJson = nlohmann::json::parse(file);
        if (m_ParentGraphJson.contains("Nodes")) {
            for (auto& nodeJson : m_ParentGraphJson["Nodes"]) {
                if (nodeJson.contains("IsParameter") && nodeJson["IsParameter"].get<bool>()) {
                    MIParameter param;
                    param.Name = nodeJson.value("ParameterName", "");
                    param.Type = nodeJson.value("Name", "");
                    param.Category = nodeJson.value("ParameterCategory", "General");

                    if (param.Type == "Float" && nodeJson.contains("FloatValue") && nodeJson["FloatValue"].is_number()) param.FloatVal = nodeJson["FloatValue"].get<float>();
                    else if (param.Type == "Color" && nodeJson.contains("ColorValue") && nodeJson["ColorValue"].is_array() && nodeJson["ColorValue"].size() >= 4) {
                        auto col = nodeJson["ColorValue"]; param.ColorVal = {col[0], col[1], col[2], col[3]};
                    }
                    else if (param.Type == "StaticSwitchParameter" && nodeJson.contains("BoolValue") && nodeJson["BoolValue"].is_boolean()) param.BoolVal = nodeJson["BoolValue"].get<bool>();
                    else if (param.Type == "Texture2D" && nodeJson.contains("TexturePath") && nodeJson["TexturePath"].is_string()) {
                        param.TexturePath = nodeJson["TexturePath"].get<std::string>();
                        if (!param.TexturePath.empty()) param.TextureID = TextureLoader::LoadTexture((Project::GetProjectDirectory() / param.TexturePath).string().c_str());
                    }
                    m_Parameters[param.Name] = param;
                }
                else if (nodeJson["Name"].get<std::string>() == "Texture2D") {
                    std::string path = nodeJson.value("TexturePath", "");
                    if (!path.empty()) {
                        MIStaticTexture st;
                        st.UniformName = "u_Tex_" + std::to_string(nodeJson["ID"].get<int>());
                        st.TextureID = TextureLoader::LoadTexture((Project::GetProjectDirectory() / path).string().c_str());
                        m_StaticTextures.push_back(st);
                    }
                }
            }
        }
        file.close();
    }
    EvaluateParameterVisibility();
}

void MaterialInstanceEditorPanel::ResetParameterToDefault(const std::string& paramName) {
    if (!m_ParentGraphJson.contains("Nodes")) return;
    for (auto& nodeJson : m_ParentGraphJson["Nodes"]) {
        if (nodeJson.contains("IsParameter") && nodeJson["IsParameter"].get<bool>() && nodeJson.value("ParameterName", "") == paramName) {
            auto& param = m_Parameters[paramName];
            if (param.Type == "Float" && nodeJson.contains("FloatValue") && nodeJson["FloatValue"].is_number()) param.FloatVal = nodeJson["FloatValue"].get<float>();
            else if (param.Type == "Color" && nodeJson.contains("ColorValue") && nodeJson["ColorValue"].is_array() && nodeJson["ColorValue"].size() >= 4) {
                auto col = nodeJson["ColorValue"]; param.ColorVal = {col[0], col[1], col[2], col[3]};
            }
            else if (param.Type == "StaticSwitchParameter" && nodeJson.contains("BoolValue") && nodeJson["BoolValue"].is_boolean()) param.BoolVal = nodeJson["BoolValue"].get<bool>();
            else if (param.Type == "Texture2D" && nodeJson.contains("TexturePath") && nodeJson["TexturePath"].is_string()) {
                param.TexturePath = nodeJson["TexturePath"].get<std::string>();
                param.TextureID = param.TexturePath.empty() ? 0 : TextureLoader::LoadTexture((Project::GetProjectDirectory() / param.TexturePath).string().c_str());
            }
            break;
        }
    }
}

void MaterialInstanceEditorPanel::EvaluateParameterVisibility() {
    for (auto& [key, param] : m_Parameters) param.IsVisible = true;
    if (!m_ParentGraphJson.contains("Nodes") || !m_ParentGraphJson.contains("Links")) return;

    std::unordered_map<int, nlohmann::json> nodesMap;
    std::unordered_map<int, int> pinToNode;
    std::unordered_map<int, int> linkEndToStart;
    int outputNodeID = -1;

    for (auto& node : m_ParentGraphJson["Nodes"]) {
        int nodeID = node["ID"];
        nodesMap[nodeID] = node;
        std::string nodeName = node.value("Name", "");
        if (nodeName == "Base Material" || nodeName == "Output" || nodeName == "PBRMaterial" || nodeName == "MaterialOutput") outputNodeID = nodeID;
        if (node.contains("Inputs")) for (auto& pin : node["Inputs"]) pinToNode[pin["ID"]] = nodeID;
        if (node.contains("Outputs")) for (auto& pin : node["Outputs"]) pinToNode[pin["ID"]] = nodeID;
    }

    for (auto& link : m_ParentGraphJson["Links"]) linkEndToStart[link["EndPinID"]] = link["StartPinID"];
    if (outputNodeID == -1) return;

    for (auto& [key, param] : m_Parameters) param.IsVisible = false;

    std::unordered_map<int, bool> visited;
    std::function<void(int)> traverse = [&](int nodeID) {
        if (visited[nodeID]) return;
        visited[nodeID] = true;
        auto& node = nodesMap[nodeID];

        if (node.value("IsParameter", false)) {
            std::string pName = node.value("ParameterName", "");
            if (m_Parameters.contains(pName)) m_Parameters[pName].IsVisible = true;
        }

        if (node.value("Name", "") == "StaticSwitchParameter") {
            std::string pName = node.value("ParameterName", "");
            bool switchVal = node.value("BoolValue", false);
            if (m_Parameters.contains(pName) && m_Parameters[pName].IsOverridden) switchVal = m_Parameters[pName].BoolVal;

            int activePinID = -1;
            if (node.contains("Inputs")) {
                for (auto& pin : node["Inputs"]) {
                    if (switchVal && pin["Name"] == "True") activePinID = pin["ID"];
                    if (!switchVal && pin["Name"] == "False") activePinID = pin["ID"];
                }
            }
            if (activePinID != -1 && linkEndToStart.contains(activePinID)) {
                if (pinToNode.contains(linkEndToStart[activePinID])) traverse(pinToNode[linkEndToStart[activePinID]]);
            }
        } else {
            if (node.contains("Inputs")) {
                for (auto& pin : node["Inputs"]) {
                    if (linkEndToStart.contains(pin["ID"]) && pinToNode.contains(linkEndToStart[pin["ID"]])) traverse(pinToNode[linkEndToStart[pin["ID"]]]);
                }
            }
        }
    };
    traverse(outputNodeID);
}

void MaterialInstanceEditorPanel::CompilePreviewShader() {
    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) return;

    if (m_ParentMaterialPath.empty()) return;
    std::ifstream file(Project::GetProjectDirectory() / m_ParentMaterialPath);
    if (!file.is_open()) return;

    nlohmann::json data;
    try { file >> data; } catch(...) { return; }

    if (data.contains("GeneratedGLSL")) {
        std::string glsl = data["GeneratedGLSL"].get<std::string>();

        // --- LE FIX EST ICI : On injecte IS_PREVIEW_VIEWPORT ---
        std::string defines = "\n#define IS_PREVIEW_VIEWPORT\n";

        for (auto& [key, param] : m_Parameters) {
            if (param.Type == "StaticSwitchParameter" && param.BoolVal) {
                defines += "#define " + key + "\n";
            }
        }

        size_t pos = glsl.find("#version");
        if (pos != std::string::npos) pos = glsl.find('\n', pos) + 1; else pos = 0;
        glsl.insert(pos, defines);

        std::filesystem::path tempPath = Project::GetCacheDirectory() / "preview_instance.frag";
        std::ofstream out(tempPath);
        out << glsl;
        out.close();

        m_PreviewShader = std::make_shared<Shader>("shaders/default.vert", tempPath.string().c_str());
    }
}

void MaterialInstanceEditorPanel::OnImGuiRender(bool& isOpen) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Material Instance Editor", &isOpen);
    ImGui::PopStyleVar();

    ImGui::Columns(2, "MI_Columns");
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.65f);
    DrawPreviewColumn();
    ImGui::NextColumn();
    DrawParameters();
    ImGui::Columns(1);
    ImGui::End();
}

void MaterialInstanceEditorPanel::RenderPreview3D(ImVec2 viewportSize) {
    m_PreviewFramebuffer->Bind();
    glViewport(0, 0, (int)viewportSize.x, (int)viewportSize.y);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_PreviewShader && m_PreviewMesh) {
        m_PreviewShader->Use();
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), viewportSize.x / viewportSize.y, 10.0f, 10000.0f);
        glm::vec3 camPos = glm::vec3(0.0f, 0.0f, m_CameraDistance);
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        m_PreviewShader->SetMat4("uProjection", proj);
        m_PreviewShader->SetMat4("uView", view);

        m_PreviewRotation += m_RotationSpeed * ImGui::GetIO().DeltaTime;
        glm::mat4 model = glm::scale(glm::rotate(glm::mat4(1.0f), glm::radians(m_PreviewRotation), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(0.8f));

        m_PreviewShader->SetMat4("uModel", model);

        // --- LE FIX DE LA LUMIÈRE EST ICI (Code propre sans doublon) ---
        m_PreviewShader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f)));
        m_PreviewShader->SetVec3("uLightColor", glm::vec3(3.0f, 3.0f, 3.0f));
        m_PreviewShader->SetVec3("uViewPos", camPos);
        m_PreviewShader->SetFloat("uTime", (float)glfwGetTime());

        // =========================================================
        // --- NOUVEAU : INJECTION DE L'IRRADIANCE MAP (IBL) ---
        // =========================================================
        glActiveTexture(GL_TEXTURE14);
        glBindTexture(GL_TEXTURE_CUBE_MAP, Renderer::GetIrradianceMapID());
        m_PreviewShader->SetInt("uIrradianceMap", 14);

        glActiveTexture(GL_TEXTURE12);
        glBindTexture(GL_TEXTURE_CUBE_MAP, Renderer::GetPrefilterMapID());
        m_PreviewShader->SetInt("uPrefilterMap", 12);

        glActiveTexture(GL_TEXTURE13);
        glBindTexture(GL_TEXTURE_2D, Renderer::GetBRDFLUTID());
        m_PreviewShader->SetInt("uBRDFLUT", 13);

        int texSlot = 0;
        for (auto& [key, param] : m_Parameters) {
            if (param.Type == "Float") m_PreviewShader->SetFloat("u_" + key, param.FloatVal);
            else if (param.Type == "Color") m_PreviewShader->SetVec4("u_" + key, param.ColorVal);
            else if (param.Type == "Texture2D" && param.TextureID != 0) {
                glActiveTexture(GL_TEXTURE0 + texSlot);
                glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)param.TextureID);
                m_PreviewShader->SetInt("u_" + key, texSlot++);
            }
        }
        for (auto& st : m_StaticTextures) {
            if (st.TextureID != 0) {
                glActiveTexture(GL_TEXTURE0 + texSlot);
                glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)st.TextureID);
                m_PreviewShader->SetInt(st.UniformName, texSlot++);
            }
        }
        m_PreviewMesh->Draw();
    }
    m_PreviewFramebuffer->Unbind();
}

void MaterialInstanceEditorPanel::DrawPreviewColumn() {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::SliderFloat("Speed", &m_RotationSpeed, -180.0f, 180.0f, "%.1f deg/s");
    ImGui::SameLine();
    ImGui::SliderFloat("Zoom", &m_CameraDistance, 50.0f, 500.0f, "%.0f cm");
    ImGui::PopItemWidth();
    ImGui::Separator();

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (m_PreviewFramebuffer && viewportSize.x > 0 && viewportSize.y > 0) {
        if (viewportSize.x != m_PreviewFramebuffer->GetSpecification().Width || viewportSize.y != m_PreviewFramebuffer->GetSpecification().Height) {
            m_PreviewFramebuffer->Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }

        // --- SÉCURITÉ VULKAN : On isole le rendu OpenGL pur ---
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            RenderPreview3D(viewportSize);
            uint32_t texID = m_PreviewFramebuffer->GetColorAttachmentRendererID();
            if (texID != 0) {
                ImGui::Image((ImTextureID)(uintptr_t)texID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));
            }
        } else {
            // Un fond gris élégant pour Vulkan en attendant !
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetCursorScreenPos().x + viewportSize.x, ImGui::GetCursorScreenPos().y + viewportSize.y),
                IM_COL32(40, 40, 40, 255)
            );
            ImGui::Dummy(viewportSize);
        }
    }
}

void MaterialInstanceEditorPanel::HandleDragAndDropParent() {
    char parentBuf[256];
    strncpy(parentBuf, m_ParentMaterialPath.c_str(), sizeof(parentBuf));
    ImGui::InputText("Parent Material", parentBuf, sizeof(parentBuf), ImGuiInputTextFlags_ReadOnly);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::filesystem::path fp = (const char*)payload->Data;
            if (fp.extension() == ".cemat") {
                MIState stateBefore = {m_ParentMaterialPath, m_Parameters};

                m_ParentMaterialPath = std::filesystem::relative(fp, Project::GetProjectDirectory()).string();
                LoadParentParameters();
                CompilePreviewShader();

                UndoManager::BeginTransaction("Assign Parent Material");
                UndoManager::PushAction(std::make_unique<MaterialInstanceCommand>(this, m_CurrentPath, stateBefore, MIState{m_ParentMaterialPath, m_Parameters}));
                UndoManager::EndTransaction();
                Save();
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void MaterialInstanceEditorPanel::DrawParameters() {
    static MIState s_StartState;
    static bool s_IsDragging = false;

    if (!s_IsDragging) s_StartState = {m_ParentMaterialPath, m_Parameters};

    std::string paramToReset = "";
    bool needsRecompileAndEval = false;
    bool immediateChange = false;
    std::string changeName = "Edit Parameter";
    MIState stateBeforeClick = {m_ParentMaterialPath, m_Parameters};

    std::map<std::string, std::vector<MIParameter*>> groupedParams;
    for (auto& [key, param] : m_Parameters) if (param.IsVisible) groupedParams[param.Category].push_back(&param);
    for (auto& [category, params] : groupedParams) {
        std::sort(params.begin(), params.end(), [](MIParameter* a, MIParameter* b) {
            if (a->Type == "StaticSwitchParameter" && b->Type != "StaticSwitchParameter") return true;
            if (a->Type != "StaticSwitchParameter" && b->Type == "StaticSwitchParameter") return false;
            return a->Name < b->Name;
        });
    }

    ImGui::Spacing(); ImGui::Spacing();

    for (auto& [category, params] : groupedParams) {
        if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto* paramPtr : params) {
                auto& param = *paramPtr;
                ImGui::PushID(param.Name.c_str());

                if (ImGui::Checkbox("##override", &param.IsOverridden)) {
                    if (!param.IsOverridden) paramToReset = param.Name;
                    if (param.Type == "StaticSwitchParameter") needsRecompileAndEval = true;
                    immediateChange = true; changeName = "Toggle Override";
                }
                ImGui::SameLine(); ImGui::Text("%s", param.Name.c_str());

                if (!param.IsOverridden) ImGui::BeginDisabled();

                if (param.Type == "StaticSwitchParameter") {
                    ImGui::SameLine(150.0f * ImGui::GetIO().FontGlobalScale);
                    if (ImGui::Checkbox("Value", &param.BoolVal)) {
                        needsRecompileAndEval = true;
                        immediateChange = true; changeName = "Toggle Switch";
                    }
                } else {
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - (10.0f * ImGui::GetIO().FontGlobalScale));
                    if (param.Type == "Float") {
                        ImGui::DragFloat("##f", &param.FloatVal, 0.01f);
                        if (ImGui::IsItemActivated()) s_IsDragging = true;
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            s_IsDragging = false; immediateChange = true; changeName = "Edit Float"; stateBeforeClick = s_StartState;
                        } else if (ImGui::IsItemDeactivated()) s_IsDragging = false;
                    } else if (param.Type == "Color") {
                        ImGui::ColorEdit4("##c", glm::value_ptr(param.ColorVal));
                        if (ImGui::IsItemActivated()) s_IsDragging = true;
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            s_IsDragging = false; immediateChange = true; changeName = "Edit Color"; stateBeforeClick = s_StartState;
                        } else if (ImGui::IsItemDeactivated()) s_IsDragging = false;
                    } else if (param.Type == "Texture2D") {
                        std::string btnText = param.TexturePath.empty() ? "Drop Texture" : std::filesystem::path(param.TexturePath).filename().string();
                        ImGui::Button(btnText.c_str(), ImVec2(-1, 24));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::filesystem::path fp = (const char*)payload->Data;
                                if (fp.extension() == ".png" || fp.extension() == ".jpg") {
                                    param.TexturePath = std::filesystem::relative(fp, Project::GetProjectDirectory()).string();
                                    param.TextureID = TextureLoader::LoadTexture(fp.string().c_str());
                                    immediateChange = true; changeName = "Assign Texture";
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                    ImGui::PopItemWidth();
                }

                if (!param.IsOverridden) ImGui::EndDisabled();
                ImGui::PopID();
                ImGui::Spacing();
            }
        }
    }

    if (!paramToReset.empty()) {
        ResetParameterToDefault(paramToReset);
        if (m_Parameters[paramToReset].Type == "StaticSwitchParameter") needsRecompileAndEval = true;
    }

    if (needsRecompileAndEval) {
        EvaluateParameterVisibility();
        CompilePreviewShader();
    }

    // Capture unifiée de toutes les modifications (Clics, Drag & Drop, Slider lâché) !
    if (immediateChange) {
        UndoManager::BeginTransaction(changeName);
        UndoManager::PushAction(std::make_unique<MaterialInstanceCommand>(this, m_CurrentPath, stateBeforeClick, MIState{m_ParentMaterialPath, m_Parameters}));
        UndoManager::EndTransaction();
        Save();
    }
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
            else if (param.Type == "StaticSwitchParameter") overrides[key] = param.BoolVal;
        }
    }
    data["Overrides"] = overrides;

    std::ofstream file(m_CurrentPath);
    if (file.is_open()) {
        file << data.dump(4);
        file.close();
        if (OnMaterialInstanceSavedCallback) OnMaterialInstanceSavedCallback(m_CurrentPath);
    }
}
