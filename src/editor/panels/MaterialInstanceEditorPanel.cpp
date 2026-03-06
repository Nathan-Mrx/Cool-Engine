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
                    // --- LE FIX DE SECURITE EST ICI ---
                    if (param.Type == "Float" && overrides[key].is_number()) {
                        param.IsOverridden = true;
                        param.FloatVal = overrides[key].get<float>();
                    }
                    else if (param.Type == "Color" && overrides[key].is_array() && overrides[key].size() >= 4) {
                        param.IsOverridden = true;
                        auto col = overrides[key];
                        param.ColorVal = {col[0], col[1], col[2], col[3]};
                    }
                    else if (param.Type == "Texture2D" && overrides[key].is_string()) {
                        param.IsOverridden = true;
                        param.TexturePath = overrides[key].get<std::string>();
                        if (!param.TexturePath.empty()) {
                            std::filesystem::path fullTex = Project::GetProjectDirectory() / param.TexturePath;
                            param.TextureID = TextureLoader::LoadTexture(fullTex.string().c_str());
                        }
                    }
                    else if (param.Type == "StaticSwitchParameter" && overrides[key].is_boolean()) {
                        param.IsOverridden = true;
                        param.BoolVal = overrides[key].get<bool>();
                    }
                }
            }
        }
        file.close();
        CompilePreviewShader();
        EvaluateParameterVisibility();
    }
}

void MaterialInstanceEditorPanel::OpenAsset(const std::filesystem::path& path) {
    Load(path);
}

void MaterialInstanceEditorPanel::LoadParentParameters() {
    m_Parameters.clear(); m_StaticTextures.clear();
    if (m_ParentMaterialPath.empty()) return;

    std::filesystem::path fullParentPath = Project::GetProjectDirectory() / m_ParentMaterialPath;
    std::ifstream file(fullParentPath);
    if (file.is_open()) {
        m_ParentGraphJson = nlohmann::json::parse(file); // ON CACHE LE JSON !
        if (m_ParentGraphJson.contains("Nodes")) {
            for (auto& nodeJson : m_ParentGraphJson["Nodes"]) {
                if (nodeJson.contains("IsParameter") && nodeJson["IsParameter"].get<bool>()) {
                    MIParameter param;
                    param.Name = nodeJson.value("ParameterName", "");
                    param.Type = nodeJson.value("Name", "");
                    param.Category = nodeJson.value("ParameterCategory", "General"); // <-- LECTURE CATÉGORIE

                    if (param.Type == "Float" && nodeJson.contains("FloatValue") && nodeJson["FloatValue"].is_number()) {
                        param.FloatVal = nodeJson["FloatValue"].get<float>();
                    }
                    else if (param.Type == "Color" && nodeJson.contains("ColorValue") && nodeJson["ColorValue"].is_array() && nodeJson["ColorValue"].size() >= 4) {
                        auto col = nodeJson["ColorValue"];
                        param.ColorVal = {col[0], col[1], col[2], col[3]};
                    }
                    else if (param.Type == "StaticSwitchParameter" && nodeJson.contains("BoolValue") && nodeJson["BoolValue"].is_boolean()) {
                        param.BoolVal = nodeJson["BoolValue"].get<bool>();
                    }
                    else if (param.Type == "Texture2D" && nodeJson.contains("TexturePath") && nodeJson["TexturePath"].is_string()) {
                        param.TexturePath = nodeJson["TexturePath"].get<std::string>();
                        if (!param.TexturePath.empty()) {
                            std::filesystem::path fullTex = Project::GetProjectDirectory() / param.TexturePath;
                            param.TextureID = TextureLoader::LoadTexture(fullTex.string().c_str());
                        }
                    }
                    m_Parameters[param.Name] = param;
                }
                else if (nodeJson["Name"].get<std::string>() == "Texture2D") {
                    std::string path = nodeJson.value("TexturePath", "");
                    if (!path.empty()) {
                        MIStaticTexture st;
                        st.UniformName = "u_Tex_" + std::to_string(nodeJson["ID"].get<int>());
                        std::filesystem::path fullTex = Project::GetProjectDirectory() / path;
                        st.TextureID = TextureLoader::LoadTexture(fullTex.string().c_str());
                        m_StaticTextures.push_back(st);
                    }
                }
            }
        }
        file.close();
    }
    EvaluateParameterVisibility(); // Calcul immédiat des visibles !
}

void MaterialInstanceEditorPanel::ResetParameterToDefault(const std::string& paramName) {
    if (!m_ParentGraphJson.contains("Nodes")) return;
    for (auto& nodeJson : m_ParentGraphJson["Nodes"]) {
        if (nodeJson.contains("IsParameter") && nodeJson["IsParameter"].get<bool>()) {
            if (nodeJson.value("ParameterName", "") == paramName) {
                auto& param = m_Parameters[paramName];
                if (param.Type == "Float" && nodeJson.contains("FloatValue") && nodeJson["FloatValue"].is_number()) {
                    param.FloatVal = nodeJson["FloatValue"].get<float>();
                }
                else if (param.Type == "Color" && nodeJson.contains("ColorValue") && nodeJson["ColorValue"].is_array() && nodeJson["ColorValue"].size() >= 4) {
                    auto col = nodeJson["ColorValue"];
                    param.ColorVal = {col[0], col[1], col[2], col[3]};
                }
                else if (param.Type == "StaticSwitchParameter" && nodeJson.contains("BoolValue") && nodeJson["BoolValue"].is_boolean()) {
                    param.BoolVal = nodeJson["BoolValue"].get<bool>();
                }
                else if (param.Type == "Texture2D" && nodeJson.contains("TexturePath") && nodeJson["TexturePath"].is_string()) {
                    param.TexturePath = nodeJson["TexturePath"].get<std::string>();
                    param.TextureID = param.TexturePath.empty() ? 0 : TextureLoader::LoadTexture((Project::GetProjectDirectory() / param.TexturePath).string().c_str());
                }
                break;
            }
        }
    }
}


void MaterialInstanceEditorPanel::EvaluateParameterVisibility() {
    // 1. On rend TOUT visible par défaut (Fail-safe)
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

        // --- FIX : AJOUTE LE NOM DE TON NOEUD FINAL ICI S'IL MANQUE ---
        if (nodeName == "Base Material" || nodeName == "Output" || nodeName == "PBRMaterial" || nodeName == "MaterialOutput") {
            outputNodeID = nodeID;
        }

        if (node.contains("Inputs")) for (auto& pin : node["Inputs"]) pinToNode[pin["ID"]] = nodeID;
        if (node.contains("Outputs")) for (auto& pin : node["Outputs"]) pinToNode[pin["ID"]] = nodeID;
    }

    for (auto& link : m_ParentGraphJson["Links"]) linkEndToStart[link["EndPinID"]] = link["StartPinID"];

    // Si on n'a pas trouvé le noeud de sortie, on retourne (tout restera visible grâce au fail-safe !)
    if (outputNodeID == -1) {
        std::cout << "[Warning] EvaluateParameterVisibility: Noeud de sortie introuvable !" << std::endl;
        return;
    }

    // 2. Maintenant qu'on est sûr que le graphe est valide, on cache tout pour le vrai tri
    for (auto& [key, param] : m_Parameters) param.IsVisible = false;

    // 3. Parcours Récursif
    std::unordered_map<int, bool> visited;
    std::function<void(int)> traverse = [&](int nodeID) {
        if (visited[nodeID]) return;
        visited[nodeID] = true;

        auto& node = nodesMap[nodeID];

        // Rendre visible si c'est un paramètre
        if (node.value("IsParameter", false)) {
            std::string pName = node.value("ParameterName", "");
            if (m_Parameters.contains(pName)) m_Parameters[pName].IsVisible = true;
        }

        // Gestion des Static Switches
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
                int startPin = linkEndToStart[activePinID];
                if (pinToNode.contains(startPin)) traverse(pinToNode[startPin]);
            }
        } else {
            if (node.contains("Inputs")) {
                for (auto& pin : node["Inputs"]) {
                    int pinID = pin["ID"];
                    if (linkEndToStart.contains(pinID)) {
                        int startPin = linkEndToStart[pinID];
                        if (pinToNode.contains(startPin)) traverse(pinToNode[startPin]);
                    }
                }
            }
        }
    };

    traverse(outputNodeID);
}
void MaterialInstanceEditorPanel::CompilePreviewShader() {
    if (m_ParentMaterialPath.empty()) return;
    std::filesystem::path fullParentPath = Project::GetProjectDirectory() / m_ParentMaterialPath;
    std::ifstream file(fullParentPath);
    if (!file.is_open()) return;

    nlohmann::json data;
    try { file >> data; } catch(...) { return; }

    if (data.contains("GeneratedGLSL")) {
        std::string glsl = data["GeneratedGLSL"].get<std::string>();

        // 1. On injecte les macros pour les Switches activés !
        std::string defines = "\n";
        for (auto& [key, param] : m_Parameters) {
            if (param.Type == "StaticSwitchParameter" && param.BoolVal) {
                defines += "#define " + key + "\n";
            }
        }

        size_t pos = glsl.find("#version");
        if (pos != std::string::npos) pos = glsl.find('\n', pos) + 1;
        else pos = 0;
        glsl.insert(pos, defines);

        // 2. On sauvegarde et compile
        std::filesystem::path cacheDir = Project::GetCacheDirectory();
        std::filesystem::path tempPath = cacheDir / "preview_instance.frag";
        std::ofstream out(tempPath);
        out << glsl;
        out.close();

        m_PreviewShader = std::make_shared<Shader>("shaders/default.vert", tempPath.string().c_str());
    }
}

// =========================================================================================
// ENTRY POINT DU RENDU UI
// =========================================================================================
void MaterialInstanceEditorPanel::OnImGuiRender(bool& isOpen) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Material Instance Editor", &isOpen);
    ImGui::PopStyleVar();

    ImGui::Columns(2, "MI_Columns");
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.65f); // 65% pour la 3D

    DrawPreviewColumn();

    ImGui::NextColumn();

    DrawDetailsColumn();

    ImGui::Columns(1);
    ImGui::End();
}

// =========================================================================================
// 1. COLONNE GAUCHE (Preview 3D)
// =========================================================================================
void MaterialInstanceEditorPanel::DrawPreviewColumn() {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::SliderFloat("Speed", &m_RotationSpeed, -180.0f, 180.0f, "%.1f deg/s");
    ImGui::SameLine();
    ImGui::SliderFloat("Zoom", &m_CameraDistance, 50.0f, 500.0f, "%.0f cm");
    ImGui::PopItemWidth();
    ImGui::Separator();

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (m_PreviewFramebuffer && viewportSize.x > 0 && viewportSize.y > 0) {

        // Resize intelligent du Framebuffer
        if (viewportSize.x != m_PreviewFramebuffer->GetSpecification().Width ||
            viewportSize.y != m_PreviewFramebuffer->GetSpecification().Height) {
            m_PreviewFramebuffer->Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }

        RenderPreview3D(viewportSize);

        // Affichage de l'image finale
        ImGui::Image((ImTextureID)(uintptr_t)m_PreviewFramebuffer->GetColorAttachmentRendererID(), viewportSize, ImVec2(0, 1), ImVec2(1, 0));
    }
}

void MaterialInstanceEditorPanel::RenderPreview3D(ImVec2 viewportSize) {
    m_PreviewFramebuffer->Bind();
    glViewport(0, 0, (int)viewportSize.x, (int)viewportSize.y);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_PreviewShader && m_PreviewMesh) {
        m_PreviewShader->Use();

        float aspect = viewportSize.x / viewportSize.y;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 10.0f, 10000.0f);
        glm::vec3 camPos = glm::vec3(0.0f, 0.0f, m_CameraDistance);
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        m_PreviewShader->SetMat4("uProjection", proj);
        m_PreviewShader->SetMat4("uView", view);

        // Rotation fluide indexée sur le delta time !
        m_PreviewRotation += m_RotationSpeed * ImGui::GetIO().DeltaTime;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, glm::radians(m_PreviewRotation), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(0.8f));
        m_PreviewShader->SetMat4("uModel", model);

        m_PreviewShader->SetVec3("uLightPos", glm::vec3(1.0f, 1.0f, 1.0f));
        m_PreviewShader->SetVec3("uLightColor", glm::vec3(3.0f, 3.0f, 3.0f));
        m_PreviewShader->SetVec3("uViewPos", camPos);
        m_PreviewShader->SetFloat("uTime", (float)glfwGetTime());

        int texSlot = 0;

        // 1. Textures des Paramètres d'Instance
        for (auto& [key, param] : m_Parameters) {
            if (param.Type == "Float") m_PreviewShader->SetFloat("u_" + key, param.FloatVal);
            else if (param.Type == "Color") m_PreviewShader->SetVec4("u_" + key, param.ColorVal);
            else if (param.Type == "Texture2D" && param.TextureID != 0) {
                glActiveTexture(GL_TEXTURE0 + texSlot);
                glBindTexture(GL_TEXTURE_2D, param.TextureID);
                m_PreviewShader->SetInt("u_" + key, texSlot);
                texSlot++;
            }
        }

        // 2. Textures Statiques du Parent
        for (auto& st : m_StaticTextures) {
            if (st.TextureID != 0) {
                glActiveTexture(GL_TEXTURE0 + texSlot);
                glBindTexture(GL_TEXTURE_2D, st.TextureID);
                m_PreviewShader->SetInt(st.UniformName, texSlot);
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
}

// =========================================================================================
// 2. COLONNE DROITE (Détails et Paramètres)
// =========================================================================================
void MaterialInstanceEditorPanel::DrawDetailsColumn() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::BeginChild("DetailsPanel");

    ImGui::TextDisabled("GENERAL");
    ImGui::Separator();

    HandleDragAndDropParent();
    DrawParameters();

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void MaterialInstanceEditorPanel::HandleDragAndDropParent() {
    char parentBuf[256];
    strncpy(parentBuf, m_ParentMaterialPath.c_str(), sizeof(parentBuf));
    ImGui::InputText("Parent Material", parentBuf, sizeof(parentBuf), ImGuiInputTextFlags_ReadOnly);

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
}

void MaterialInstanceEditorPanel::DrawParameters() {
    std::string paramToReset = "";
    bool needsRecompileAndEval = false;

    // Tri par catégories et par type (Switches en premier)
    std::map<std::string, std::vector<MIParameter*>> groupedParams;
    for (auto& [key, param] : m_Parameters) {
        if (param.IsVisible) {
            groupedParams[param.Category].push_back(&param);
        }
    }

    for (auto& [category, params] : groupedParams) {
        std::sort(params.begin(), params.end(), [](MIParameter* a, MIParameter* b) {
            if (a->Type == "StaticSwitchParameter" && b->Type != "StaticSwitchParameter") return true;
            if (a->Type != "StaticSwitchParameter" && b->Type == "StaticSwitchParameter") return false;
            return a->Name < b->Name; // Ordre alphabétique
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
                }
                ImGui::SameLine(); ImGui::Text("%s", param.Name.c_str());

                if (!param.IsOverridden) ImGui::BeginDisabled();

                if (param.Type == "StaticSwitchParameter") {
                    ImGui::SameLine(150);
                    if (ImGui::Checkbox("Value", &param.BoolVal)) needsRecompileAndEval = true;
                } else {
                    ImGui::PushItemWidth(-1);
                    if (param.Type == "Float") {
                        ImGui::DragFloat("##f", &param.FloatVal, 0.01f);
                    } else if (param.Type == "Color") {
                        ImGui::ColorEdit4("##c", glm::value_ptr(param.ColorVal));
                    } else if (param.Type == "Texture2D") {
                        std::string btnText = param.TexturePath.empty() ? "Drop Texture" : std::filesystem::path(param.TexturePath).filename().string();
                        ImGui::Button(btnText.c_str(), ImVec2(-1, 24));
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
                }

                if (!param.IsOverridden) ImGui::EndDisabled();
                ImGui::PopID();
                ImGui::Spacing();
            }
        }
    }

    // Résolution des actions (Évite les crashs d'itérateurs UI !)
    if (!paramToReset.empty()) {
        ResetParameterToDefault(paramToReset);
        if (m_Parameters[paramToReset].Type == "StaticSwitchParameter") needsRecompileAndEval = true;
    }

    if (needsRecompileAndEval) {
        EvaluateParameterVisibility();
        CompilePreviewShader();
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
        std::cout << "[Editor] Saved Material Instance: " << m_CurrentPath.filename() << std::endl;
        
        if (OnMaterialInstanceSavedCallback) {
            OnMaterialInstanceSavedCallback(m_CurrentPath);
        }
    }
}
