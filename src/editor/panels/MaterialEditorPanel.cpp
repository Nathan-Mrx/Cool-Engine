#include "MaterialEditorPanel.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include "renderer/TextureLoader.h"

#include "../../renderer/PrimitiveFactory.h"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <nfd.h>
#include <nfd.hpp>

#include "editor/materials/MaterialNodeRegistry.h"
#include "project/Project.h"
#include "renderer/Renderer.h"
#include "renderer/VulkanRenderer.h"

static void DrawPinIcon(PinType type, bool connected) {
    ImVec2 size(24, 14); // Plus large pour la marge

    if (ImGui::IsRectVisible(size)) {
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Position du cercle (o)
        ImVec2 center = ImVec2(cursorPos.x + 6, cursorPos.y + 7);

        ImVec4 color;
        switch (type) {
        case PinType::Float: color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break;
        case PinType::Vec2:  color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); break;
        case PinType::Vec3:  color = ImVec4(0.2f, 0.4f, 0.9f, 1.0f); break;
        case PinType::Vec4:  color = ImVec4(0.8f, 0.2f, 0.8f, 1.0f); break;
        }
        ImU32 color32 = ImGui::GetColorU32(color);

        if (connected) {
            drawList->AddCircleFilled(center, 5.0f, color32);
        } else {
            drawList->AddCircle(center, 5.0f, color32, 0, 2.0f);
            drawList->AddCircleFilled(center, 3.0f, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.2f)));
        }

        // 2. Position du triangle (>) avec MARGE de 4 pixels
        // On le décale à +16 pixels du début
        ImVec2 p1(cursorPos.x + 16, cursorPos.y + 3);
        ImVec2 p2(cursorPos.x + 16, cursorPos.y + 11);
        ImVec2 p3(cursorPos.x + 21, cursorPos.y + 7);
        drawList->AddTriangleFilled(p1, p2, p3, color32);
    }
    ImGui::Dummy(size);
}


MaterialEditorPanel::MaterialEditorPanel() {
    // 1. On charge tous les noeuds générés par le script Python !
    MaterialNodeRegistry::RegisterAllNodes();

    ed::Config config;
    config.SettingsFile = nullptr; // Bloque la corruption de caméra
    m_Context = ed::CreateEditor(&config);

    // 2. On active le contexte pour TOUTE la durée de l'initialisation
    ed::SetCurrentEditor(m_Context);
    ed::Style& style = ed::GetStyle();

    // Formes et épaisseurs
    style.NodeRounding = 8.0f;
    style.PinRounding = 4.0f;
    style.LinkStrength = 100.0f; // Câbles tendus
    style.NodeBorderWidth = 1.5f;
    style.HoveredNodeBorderWidth = 2.5f;
    style.SelectedNodeBorderWidth = 3.0f;
    style.PinBorderWidth = 1.0f;

    // Couleurs
    style.Colors[ed::StyleColor_Bg]             = ImColor(30, 30, 30, 255);
    style.Colors[ed::StyleColor_Grid]           = ImColor(50, 50, 50, 100);
    style.Colors[ed::StyleColor_NodeBg]         = ImColor(35, 35, 35, 255);
    style.Colors[ed::StyleColor_NodeBorder]     = ImColor(30, 30, 30, 255);
    style.Colors[ed::StyleColor_HovNodeBorder]  = ImColor(80, 120, 200, 255);
    style.Colors[ed::StyleColor_SelNodeBorder]  = ImColor(255, 165, 0, 255);
    style.Colors[ed::StyleColor_HovLinkBorder]  = ImColor(80, 120, 200, 255);
    style.Colors[ed::StyleColor_SelLinkBorder]  = ImColor(255, 165, 0, 255);
    style.Colors[ed::StyleColor_PinRect]        = ImColor(80, 120, 200, 255);
    style.Colors[ed::StyleColor_PinRectBorder]  = ImColor(80, 120, 200, 255);

    // 3. On construit les noeuds PENDANT que le contexte est actif
    BuildDefaultNodes();

    // =========================================================
    // --- LE RETOUR DU VIEWPORT 3D ---
    // =========================================================
    FramebufferSpecification fbSpec;
    fbSpec.Width = 512;
    fbSpec.Height = 512;
    m_PreviewFramebuffer = Framebuffer::Create(fbSpec);

    m_PreviewMesh = PrimitiveFactory::CreateSphere();

    CompilePreviewShader();

    // 4. On peut refermer le contexte en toute sécurité
    ed::SetCurrentEditor(nullptr);
}

MaterialEditorPanel::~MaterialEditorPanel() {
    // Wait for the GPU to finish any in-flight frames before destroying our framebuffers and meshes
    if (Renderer::GetAPI() == RendererAPI::API::Vulkan) {
        vkDeviceWaitIdle(VulkanRenderer::Get()->GetDevice());
    }
    ed::DestroyEditor(m_Context);
}

void MaterialEditorPanel::BuildDefaultNodes() {
    m_Nodes.clear();
    m_Links.clear();
    m_NextId = 1;

    // --- L'AIRBAG MÉMOIRE : On prévient le déplacement des Noeuds ---
    m_Nodes.reserve(100);

    MaterialNode baseNode;
    if (MaterialNodeRegistry::CreateNode("Base Material", m_NextId, baseNode)) {
        m_Nodes.push_back(baseNode);
        ed::SetNodePosition(m_Nodes.back().ID, ImVec2(400, 100)); // Toujours positionner la copie finale !
    }

    MaterialNode colorNode;
    if (MaterialNodeRegistry::CreateNode("Color", m_NextId, colorNode)) {
        m_Nodes.push_back(colorNode);
        ed::SetNodePosition(m_Nodes.back().ID, ImVec2(50, 100));

        // Branchement par défaut
        if (!m_Nodes[1].Outputs.empty() && !m_Nodes[0].Inputs.empty()) {
            m_Links.push_back({ ed::LinkId(m_NextId++), m_Nodes[1].Outputs[0].ID, m_Nodes[0].Inputs[0].ID });
        }
    }
}

// =========================================================================================
// ENTRY POINT DU RENDU UI
// =========================================================================================
void MaterialEditorPanel::OnImGuiRender(bool& isOpen) {
    if (!isOpen) return;

    // Mise à jour du temps global pour les shaders
    m_TotalTime += ImGui::GetIO().DeltaTime;

    DrawPreviewWindow();
    DrawNodeEditorWindow(isOpen);
}

// =========================================================================================
// 1. RENDU OPENGL (La Sphère PBR)
// =========================================================================================
void MaterialEditorPanel::RenderPreview3D() {
    if (!m_PreviewFramebuffer) return;

    m_PreviewFramebuffer->Bind();

    // --- SÉCURITÉ VULKAN : On enferme TOUT OpenGL ---
    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        glViewport(0, 0, m_PreviewFramebuffer->GetSpecification().Width, m_PreviewFramebuffer->GetSpecification().Height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (m_PreviewShader && m_PreviewMesh) {
            m_PreviewShader->Use();

            float width = (float)m_PreviewFramebuffer->GetSpecification().Width;
            float height = (float)m_PreviewFramebuffer->GetSpecification().Height;
            float aspect = (height > 0.0f) ? (width / height) : 1.0f;

            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 10.0f, 10000.0f);
            glm::vec3 camPos = glm::vec3(0.0f, 0.0f, m_CameraDistance);
            glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

            m_PreviewShader->SetMat4("uProjection", proj);
            m_PreviewShader->SetMat4("uView", view);

            m_PreviewRotation += m_RotationSpeed * ImGui::GetIO().DeltaTime;
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::rotate(model, glm::radians(m_PreviewRotation), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.8f));
            m_PreviewShader->SetMat4("uModel", model);

            m_PreviewShader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f)));
            m_PreviewShader->SetVec3("uLightColor", glm::vec3(3.0f, 3.0f, 3.0f));
            m_PreviewShader->SetVec3("uViewPos", camPos);
            m_PreviewShader->SetFloat("uTime", m_TotalTime);

            glActiveTexture(GL_TEXTURE14);
            glBindTexture(GL_TEXTURE_CUBE_MAP, Renderer::GetIrradianceMapID());
            m_PreviewShader->SetInt("uIrradianceMap", 14);

            glActiveTexture(GL_TEXTURE12);
            glBindTexture(GL_TEXTURE_CUBE_MAP, Renderer::GetPrefilterMapID());
            m_PreviewShader->SetInt("uPrefilterMap", 12);

            glActiveTexture(GL_TEXTURE13);
            glBindTexture(GL_TEXTURE_2D, Renderer::GetBRDFLUTID());
            m_PreviewShader->SetInt("uBRDFLUT", 13);

            int slot = 0;
            for (auto& node : m_Nodes) {
                if (node.Name == "Texture2D" && node.TextureID != 0) {
                    glActiveTexture(GL_TEXTURE0 + slot);
                    glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)node.TextureID);
                    if (node.IsParameter) m_PreviewShader->SetInt("u_" + node.ParameterName, slot);
                    else m_PreviewShader->SetInt("u_Tex_" + std::to_string((int)node.ID.Get()), slot);
                    slot++;
                }

                if (node.IsParameter) {
                    if (node.Name == "Float") m_PreviewShader->SetFloat("u_" + node.ParameterName, node.FloatValue);
                    if (node.Name == "Color") m_PreviewShader->SetVec4("u_" + node.ParameterName, node.ColorValue);
                }
            }

            glBindVertexArray(m_PreviewMesh->GetVAO());
            glDrawElements(GL_TRIANGLES, m_PreviewMesh->GetIndicesCount(), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);

            for (int i = 0; i < 8; i++) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            glUseProgram(0);
        }
    } else {
        // --- MODE VULKAN (Isolé et Sécurisé) ---
        if (m_PreviewMesh && m_PreviewFramebuffer) {
            float width = (float)m_PreviewFramebuffer->GetSpecification().Width;
            float height = (float)m_PreviewFramebuffer->GetSpecification().Height;
            float aspect = (height > 0.0f) ? (width / height) : 1.0f;

            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 10.0f, 10000.0f);
            proj[1][1] *= -1.0f;

            glm::vec3 camPos = glm::vec3(m_CameraDistance, 0.0f, 0.0f);
            glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

            m_PreviewRotation += m_RotationSpeed * ImGui::GetIO().DeltaTime;
            glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(m_PreviewRotation), glm::vec3(0.0f, 0.0f, 1.0f));

            // Traceur Live
            VulkanTexture* t_albedo = nullptr; VulkanTexture* t_normal = nullptr;
            VulkanTexture* t_metallic = nullptr; VulkanTexture* t_roughness = nullptr; VulkanTexture* t_ao = nullptr;
            glm::vec4 c_color = glm::vec4(1.0f); float v_met = 0.0f, v_rough = 0.5f, v_ao = 1.0f;

            MaterialNode* baseNode = nullptr;
            for (auto& n : m_Nodes) { if (n.Name == "Base Material") { baseNode = &n; break; } }

            if (baseNode && baseNode->Inputs.size() >= 6) {

                std::function<VulkanTexture*(ed::PinId)> traceTexRec = [&](ed::PinId pinId) -> VulkanTexture* {
                    for (auto& link : m_Links) {
                        if (link.EndPinID == pinId) {
                            MaterialPin* p = FindPin(link.StartPinID);
                            if (p) {
                                MaterialNode* src = FindNode(p->NodeID);
                                if (!src) continue;

                                if (src->Name == "Texture2D") {
                                    if (src->TextureID == nullptr && !src->TexturePath.empty()) {
                                        src->TextureID = TextureLoader::LoadTexture(src->TexturePath.c_str());
                                    }
                                    return (VulkanTexture*)src->TextureID;
                                }

                                if (src->Name == "StaticSwitchParameter" || src->Name == "Static Switch Parameter") {
                                    if (src->Inputs.size() >= 2) {
                                        int activeIdx = src->BoolValue ? 0 : 1;
                                        return traceTexRec(src->Inputs[activeIdx].ID);
                                    }
                                }

                                for (auto& inPin : src->Inputs) {
                                    VulkanTexture* res = traceTexRec(inPin.ID);
                                    if (res) return res;
                                }
                            }
                        }
                    }
                    return nullptr;
                };

                std::function<glm::vec4(ed::PinId, glm::vec4)> traceColorRec = [&](ed::PinId pinId, glm::vec4 def) -> glm::vec4 {
                    for (auto& link : m_Links) {
                        if (link.EndPinID == pinId) {
                            MaterialPin* p = FindPin(link.StartPinID);
                            if (p) {
                                MaterialNode* src = FindNode(p->NodeID);
                                if (!src) continue;

                                if (src->Name == "Color") return glm::vec4(src->ColorValue.r, src->ColorValue.g, src->ColorValue.b, src->ColorValue.a);

                                if (src->Name == "StaticSwitchParameter" || src->Name == "Static Switch Parameter") {
                                    if (src->Inputs.size() >= 2) {
                                        int activeIdx = src->BoolValue ? 0 : 1;
                                        return traceColorRec(src->Inputs[activeIdx].ID, def);
                                    }
                                }

                                for (auto& inPin : src->Inputs) {
                                    glm::vec4 res = traceColorRec(inPin.ID, glm::vec4(-1.0f));
                                    if (res.x != -1.0f) return res;
                                }
                            }
                        }
                    }
                    return def;
                };

                std::function<float(ed::PinId, float)> traceFloatRec = [&](ed::PinId pinId, float def) -> float {
                    for (auto& link : m_Links) {
                        if (link.EndPinID == pinId) {
                            MaterialPin* p = FindPin(link.StartPinID);
                            if (p) {
                                MaterialNode* src = FindNode(p->NodeID);
                                if (!src) continue;

                                if (src->Name == "Float") return src->FloatValue;

                                if (src->Name == "StaticSwitchParameter" || src->Name == "Static Switch Parameter") {
                                    if (src->Inputs.size() >= 2) {
                                        int activeIdx = src->BoolValue ? 0 : 1;
                                        return traceFloatRec(src->Inputs[activeIdx].ID, def);
                                    }
                                }

                                for (auto& inPin : src->Inputs) {
                                    float res = traceFloatRec(inPin.ID, -9999.0f);
                                    if (res != -9999.0f) return res;
                                }
                            }
                        }
                    }
                    return def;
                };

                t_albedo = traceTexRec(baseNode->Inputs[0].ID);
                t_normal = traceTexRec(baseNode->Inputs[1].ID);
                t_metallic = traceTexRec(baseNode->Inputs[2].ID);
                t_roughness = traceTexRec(baseNode->Inputs[3].ID);
                t_ao = traceTexRec(baseNode->Inputs[5].ID);

                c_color = traceColorRec(baseNode->Inputs[0].ID, glm::vec4(1.0f));
                v_met = traceFloatRec(baseNode->Inputs[2].ID, 0.0f);
                v_rough = traceFloatRec(baseNode->Inputs[3].ID, 0.5f);
                v_ao = traceFloatRec(baseNode->Inputs[5].ID, 1.0f);
            }

            VulkanRenderer::Get()->RenderMaterialPreview(
                m_PreviewMesh.get(), (VulkanFramebuffer*)m_PreviewFramebuffer.get(),
                model, view, proj, camPos,
                t_albedo, t_normal, t_metallic, t_roughness, t_ao,
                c_color, v_met, v_rough, v_ao
            );
        }
    }

    m_PreviewFramebuffer->Unbind();
}

// =========================================================================================
// 2. FENETRE IMGUI DE LA PREVIEW
// =========================================================================================
void MaterialEditorPanel::DrawPreviewWindow() {
    ImGui::Begin("Material Preview");

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::SliderFloat("Speed", &m_RotationSpeed, -180.0f, 180.0f, "%.1f deg/s");
    ImGui::SameLine();
    ImGui::SliderFloat("Zoom", &m_CameraDistance, 50.0f, 1000.0f, "%.0f cm");
    ImGui::PopItemWidth();
    ImGui::Separator();

    ImVec2 previewAvail = ImGui::GetContentRegionAvail();
    if (previewAvail.x > 0 && previewAvail.y > 0) {
        // On mémorise la taille demandée par ImGui pour la Frame SUIVANTE
        m_ViewportSize = glm::vec2(previewAvail.x, previewAvail.y);

        if (m_PreviewFramebuffer) {
            void* texID = m_PreviewFramebuffer->GetColorAttachmentRendererID();
            if (texID != nullptr) {
                ImVec2 uv0 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(0, 1) : ImVec2(0, 0);
                ImVec2 uv1 = RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? ImVec2(1, 0) : ImVec2(1, 1);
                ImGui::Image((ImTextureID)texID, previewAvail, uv0, uv1);
            } else {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetCursorScreenPos(),
                    ImVec2(ImGui::GetCursorScreenPos().x + previewAvail.x, ImGui::GetCursorScreenPos().y + previewAvail.y),
                    IM_COL32(40, 40, 40, 255)
                );
                ImGui::Dummy(previewAvail);
            }
        }
    }
    ImGui::End();
}

// =========================================================================================
// 3. FENETRE PRINCIPALE DE L'EDITEUR NODAL
// =========================================================================================
void MaterialEditorPanel::DrawNodeEditorWindow(bool& isOpen) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("Material Editor", &isOpen)) {
        float scale = ImGui::GetIO().FontGlobalScale;

        // 1. La barre d'outils du haut DOIT être scalée (c'est de l'UI classique)
        ImGui::SetCursorPos(ImVec2(10.0f * scale, 10.0f * scale));
        if (ImGui::Button("Force Update Preview", ImVec2(150 * scale, 30 * scale))) CompilePreviewShader();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Preview is Live. Press Ctrl+S to Apply to Scene.");

        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 100.0f * scale) avail.x = 800.0f * scale;
        if (avail.y < 100.0f * scale) avail.y = 600.0f * scale;

        // =====================================================================
        // BOUCLIER ANTI-SCALE : On isole le Canvas Nodal de l'échelle globale
        // =====================================================================
        ImGuiStyle oldStyle = ImGui::GetStyle(); // On sauvegarde le style actuel

        ImGuiStyle& style = ImGui::GetStyle();
        style.FramePadding.x /= scale;
        style.FramePadding.y /= scale;
        style.ItemSpacing.x /= scale;
        style.ItemSpacing.y /= scale;
        style.ItemInnerSpacing.x /= scale;
        style.ItemInnerSpacing.y /= scale;

        // On force le texte de cette fenêtre à s'afficher à l'échelle 1.0x
        ImGui::SetWindowFontScale(1.0f / scale);
        // =====================================================================

        ed::SetCurrentEditor(m_Context);
        ed::Begin("My Material Graph", avail);

        UpdateWildcardPins();
        HandleShortcuts();
        DrawNodes();
        DrawLinks();
        HandleInteraction();

        // --- PAUSE DU BOUCLIER POUR LES MENUS ---
        // Les popups (clic droit) sont de l'UI classique, on veut qu'ils soient grands !
        ImGui::SetWindowFontScale(1.0f);
        ImGui::GetStyle() = oldStyle;

        DrawContextMenus();

        // --- REMISE DU BOUCLIER ---
        ImGui::SetWindowFontScale(1.0f / scale);
        style = ImGui::GetStyle();
        style.FramePadding.x /= scale;
        style.FramePadding.y /= scale;
        style.ItemSpacing.x /= scale;
        style.ItemSpacing.y /= scale;
        style.ItemInnerSpacing.x /= scale;
        style.ItemInnerSpacing.y /= scale;
        // ----------------------------------------

        if (m_FirstFrame && m_Nodes.size() >= 2) {
            ed::SetNodePosition(m_Nodes[0].ID, ImVec2(400, 100));
            ed::SetNodePosition(m_Nodes[1].ID, ImVec2(50, 100));
            m_FirstFrame = false;
        }

        ed::End();
        ed::SetCurrentEditor(nullptr);

        // =====================================================================
        // FIN DU BOUCLIER : Restauration de l'état normal pour le reste de l'UI
        // =====================================================================
        ImGui::SetWindowFontScale(1.0f);
        ImGui::GetStyle() = oldStyle;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// =========================================================================================
// LOGIQUE INTERNE DE L'EDITEUR NODAL
// =========================================================================================

void MaterialEditorPanel::HandleShortcuts() {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_C) && !ImGui::GetIO().WantTextInput) {
        int selectedCount = ed::GetSelectedObjectCount();
        if (selectedCount > 0) {
            std::vector<ed::NodeId> selectedNodes(selectedCount);
            ed::GetSelectedNodes(selectedNodes.data(), selectedCount);

            glm::vec2 min(999999.0f, 999999.0f);
            glm::vec2 max(-999999.0f, -999999.0f);

            for (auto& id : selectedNodes) {
                MaterialNode* n = FindNode(id);
                if (n && n->Name != "Comment") {
                    ImVec2 pos = ed::GetNodePosition(id);
                    ImVec2 size = ed::GetNodeSize(id);
                    if (pos.x < min.x) min.x = pos.x;
                    if (pos.y < min.y) min.y = pos.y;
                    if (pos.x + size.x > max.x) max.x = pos.x + size.x;
                    if (pos.y + size.y > max.y) max.y = pos.y + size.y;
                }
            }

            if (min.x < max.x) {
                min.x -= 30; min.y -= 50;
                max.x += 30; max.y += 30;

                MaterialNode commentNode;
                commentNode.ID = ed::NodeId(m_NextId++);
                commentNode.Name = "Comment";
                commentNode.CommentText = "New Comment";
                commentNode.ColorValue = { 1.0f, 1.0f, 1.0f, 0.1f };
                commentNode.Size = { max.x - min.x, max.y - min.y };

                m_Nodes.push_back(commentNode);
                ed::SetNodePosition(commentNode.ID, ImVec2(min.x, min.y));
            }
        }
    }
}

void MaterialEditorPanel::DrawNodes() {
    for (auto& node : m_Nodes) {
        if (node.Name == "Comment") DrawCommentNode(node);
        else if (node.Name == "Reroute") DrawRerouteNode(node);
        else DrawStandardNode(node);
    }
}

void MaterialEditorPanel::DrawCommentNode(MaterialNode& node) {
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(node.ColorValue.r, node.ColorValue.g, node.ColorValue.b, node.ColorValue.a));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(node.ColorValue.r, node.ColorValue.g, node.ColorValue.b, 0.8f));

    ed::BeginNode(node.ID);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::TextUnformatted(node.CommentText.c_str());
    ImGui::PopStyleColor();

    ed::Group(ImVec2(node.Size.x, node.Size.y));
    ed::EndNode();
    ed::PopStyleColor(2);

    static std::unordered_map<int, ImVec2> s_CommentPadding;

    if (!m_FirstFrame) {
        ImVec2 totalSize = ed::GetNodeSize(node.ID);

        if (s_CommentPadding.find((int)node.ID.Get()) == s_CommentPadding.end()) {
            s_CommentPadding[(int)node.ID.Get()] = ImVec2(totalSize.x - node.Size.x, totalSize.y - node.Size.y);
        }

        ImVec2 padding = s_CommentPadding[(int)node.ID.Get()];
        if (totalSize.x > 0 && totalSize.y > 0) {
            node.Size = { totalSize.x - padding.x, totalSize.y - padding.y };
        }
    }
}

void MaterialEditorPanel::DrawRerouteNode(MaterialNode& node) {
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0, 0, 0, 0));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0, 0, 0, 0));
    ed::BeginNode(node.ID);

    bool isConnectedIn = false, isConnectedOut = false;
    for (auto& link : m_Links) {
        if (link.EndPinID == node.Inputs[0].ID) isConnectedIn = true;
        if (link.StartPinID == node.Outputs[0].ID) isConnectedOut = true;
    }

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 center(cursorPos.x + 12, cursorPos.y + 7);
    ImVec4 color;
    switch (node.Inputs[0].Type) {
        case PinType::Float: color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break;
        case PinType::Vec2:  color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); break;
        case PinType::Vec3:  color = ImVec4(0.2f, 0.4f, 0.9f, 1.0f); break;
        case PinType::Vec4:  color = ImVec4(0.8f, 0.2f, 0.8f, 1.0f); break;
    }
    ImU32 color32 = ImGui::GetColorU32(color);

    if (isConnectedIn || isConnectedOut) drawList->AddCircleFilled(center, 5.0f, color32);
    else {
        drawList->AddCircle(center, 5.0f, color32, 0, 2.0f);
        drawList->AddCircleFilled(center, 3.0f, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.2f)));
    }

    ImGui::BeginGroup();
    ed::BeginPin(node.Inputs[0].ID, node.Inputs[0].Kind); ImGui::Dummy(ImVec2(12, 14)); ed::EndPin();
    ImGui::SameLine(0, 0);
    ed::BeginPin(node.Outputs[0].ID, node.Outputs[0].Kind); ImGui::Dummy(ImVec2(12, 14)); ed::EndPin();
    ImGui::EndGroup();

    ed::EndNode();
    ed::PopStyleColor(2);
}

void MaterialEditorPanel::DrawStandardNode(MaterialNode& node) {
    ed::BeginNode(node.ID);

    // Header
    if (node.IsParameter) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImGui::TextUnformatted((node.ParameterName + " (Param)").c_str());
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::TextUnformatted(node.Name.c_str());
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Inputs
    ImGui::BeginGroup();
    for (auto& input : node.Inputs) {
        bool isConnected = false;
        for (auto& link : m_Links) if (link.EndPinID == input.ID) { isConnected = true; break; }

        ed::BeginPin(input.ID, input.Kind);
        DrawPinIcon(input.Type, isConnected);
        ImGui::SameLine(0, 6);
        ImGui::TextUnformatted(input.Name.c_str());
        ed::EndPin();

        if (!isConnected) {
            ImGui::SameLine(0, 6);
            ImGui::PushID((int)input.ID.Get());
            if (input.Type == PinType::Float) {
                ImGui::PushItemWidth(60.0f);
                ImGui::DragFloat("##v", &input.FloatValue, 0.01f);
                if (ImGui::IsItemDeactivatedAfterEdit()) CompilePreviewShader();
                ImGui::PopItemWidth();
            } else if (input.Type == PinType::Vec2) {
                ImGui::PushItemWidth(100.0f);
                ImGui::DragFloat2("##v", &input.Vec2Value[0], 0.01f);
                if (ImGui::IsItemDeactivatedAfterEdit()) CompilePreviewShader();
                ImGui::PopItemWidth();
            } else if (input.Type == PinType::Vec3) {
                ImGui::PushItemWidth(60.0f);
                ImGui::ColorEdit3("##v", &input.Vec3Value[0], ImGuiColorEditFlags_NoInputs);
                if (ImGui::IsItemDeactivatedAfterEdit()) CompilePreviewShader();
                ImGui::PopItemWidth();
            }
            ImGui::PopID();
        }
    }

    // Propriétés spécifiques du noeud
    ImGui::PushID((int)node.ID.Get());
    if (node.Name == "Color") {
        ImGui::PushItemWidth(120.0f);
        ImGui::ColorEdit4("##val", &node.ColorValue[0], ImGuiColorEditFlags_NoInputs);
        if (ImGui::IsItemDeactivatedAfterEdit()) CompilePreviewShader();
        ImGui::PopItemWidth();
    } else if (node.Name == "Float") {
        ImGui::PushItemWidth(80.0f);
        ImGui::DragFloat("##val", &node.FloatValue, 0.01f);
        if (ImGui::IsItemDeactivatedAfterEdit()) CompilePreviewShader();
        ImGui::PopItemWidth();
    } else if (node.Name == "Texture2D") {
        ImGui::PushItemWidth(120.0f);
        if (node.TexturePath.empty()) ImGui::Button("Drop Texture", ImVec2(120, 30));
        else {
            if (node.TextureID == 0) node.TextureID = TextureLoader::LoadTexture(node.TexturePath.c_str());

            if (node.TextureID != 0) {
                void* imguiTexID = node.TextureID;
                // FIX VULKAN : ImGui a besoin de son DescriptorSet spécial, pas du pointeur de la structure !
                if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                    imguiTexID = ((VulkanTexture*)node.TextureID)->ImGuiDescriptor;
                }
                ImGui::Image((ImTextureID)imguiTexID, ImVec2(120, 120), ImVec2(0, 1), ImVec2(1, 0));
            }
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                std::filesystem::path fp = (const char*)payload->Data;
                if (fp.extension() == ".png" || fp.extension() == ".jpg") {
                    node.TexturePath = std::filesystem::relative(fp, Project::GetProjectDirectory()).string();
                    node.TextureID = TextureLoader::LoadTexture(fp.string().c_str());
                    CompilePreviewShader();
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopItemWidth();
    }
    ImGui::PopID();
    ImGui::EndGroup();

    // Outputs
    if (!node.Outputs.empty()) {
        ImGui::SameLine(0, 40.0f);
        ImGui::BeginGroup();
        float maxOutputWidth = 0.0f;
        for (auto& output : node.Outputs) {
            float w = ImGui::CalcTextSize(output.Name.c_str()).x;
            if (w > maxOutputWidth) maxOutputWidth = w;
        }
        for (auto& output : node.Outputs) {
            bool isConnected = false;
            for (auto& link : m_Links) if (link.StartPinID == output.ID) { isConnected = true; break; }
            float textWidth = ImGui::CalcTextSize(output.Name.c_str()).x;
            float padding = maxOutputWidth - textWidth;

            ed::BeginPin(output.ID, output.Kind);
            if (padding > 0) { ImGui::Dummy(ImVec2(padding, 0)); ImGui::SameLine(0, 0); }
            ImGui::TextUnformatted(output.Name.c_str());
            ImGui::SameLine(0, 6);
            DrawPinIcon(output.Type, isConnected);
            ed::EndPin();
        }
        ImGui::EndGroup();
    }

    ed::EndNode();

    // Rendu esthétique de l'en-tête (Background couleur)
    if (ImGui::IsItemVisible()) {
        ImVec2 nodeMin = ImGui::GetItemRectMin();
        ImVec2 nodeMax = ImGui::GetItemRectMax();
        auto drawList = ed::GetNodeBackgroundDrawList(node.ID);

        // On augmente la hauteur à 16.0f au lieu de 8.0f pour couvrir le padding naturel
        float headerHeight = ImGui::GetTextLineHeight() + 16.0f;
        ImVec2 headerMax = ImVec2(nodeMax.x, nodeMin.y + headerHeight);
        ImColor headerColor = MaterialNodeRegistry::GetNodeColor(node.Name);

        drawList->AddRectFilled(nodeMin, headerMax, headerColor, ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
        drawList->AddLine(ImVec2(nodeMin.x, headerMax.y), headerMax, ImColor(30, 30, 30, 255), 2.0f);
    }
}

void MaterialEditorPanel::DrawLinks() {
    for (auto& link : m_Links) {
        MaterialPin* startPin = FindPin(link.StartPinID);
        ImVec4 linkColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        if (startPin) {
            switch (startPin->Type) {
                case PinType::Float: linkColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break;
                case PinType::Vec2:  linkColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); break;
                case PinType::Vec3:  linkColor = ImVec4(0.2f, 0.4f, 0.9f, 1.0f); break;
                case PinType::Vec4:  linkColor = ImVec4(0.8f, 0.2f, 0.8f, 1.0f); break;
            }
        }
        ed::Link(link.ID, link.StartPinID, link.EndPinID, linkColor, 2.5f);
    }
}

void MaterialEditorPanel::HandleInteraction() {
    // 1. CREATION
    if (ed::BeginCreate(ImVec4(0.3f, 0.5f, 0.8f, 1.0f), 2.5f)) {
        ed::PinId startPinId = 0, endPinId = 0;
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            auto startPin = FindPin(startPinId);
            auto endPin = FindPin(endPinId);
            if (startPin && endPin) {
                if (startPin == endPin || startPin->NodeID == endPin->NodeID || startPin->Kind == endPin->Kind) {
                    ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                } else if (ed::AcceptNewItem(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 2.0f)) {
                    if (startPin->Kind == ed::PinKind::Input) {
                        std::swap(startPin, endPin);
                        std::swap(startPinId, endPinId);
                    }
                    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                        [endPinId](const MaterialLink& link) { return link.EndPinID == endPinId; }), m_Links.end());
                    m_Links.push_back({ ed::LinkId(GetNextId()), startPinId, endPinId });
                    CompilePreviewShader();
                }
            }
        }
        ed::PinId newNodePinId = 0;
        if (ed::QueryNewNode(&newNodePinId)) {
            if (ed::AcceptNewItem()) {
                m_NewNodeLinkPinId = newNodePinId;
                m_ContextPopupPos = ed::ScreenToCanvas(ImGui::GetMousePos());
                m_RequestNodeMenu = true; // Déclenche le menu à la prochaine étape !
            }
        }
    }
    ed::EndCreate();

    // 2. SUPPRESSION
    if (ed::BeginDelete()) {
        bool hasDeleted = false;
        ed::LinkId deletedLinkId = 0;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            if (ed::AcceptDeletedItem()) {
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [deletedLinkId](const MaterialLink& link) { return link.ID == deletedLinkId; }), m_Links.end());
                hasDeleted = true;
            }
        }
        ed::NodeId deletedNodeId = 0;
        while (ed::QueryDeletedNode(&deletedNodeId)) {
            auto node = FindNode(deletedNodeId);
            if (node && node->Name == "Base Material") ed::RejectDeletedItem();
            else if (ed::AcceptDeletedItem()) {
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [this, deletedNodeId](const MaterialLink& link) {
                        MaterialPin* p1 = FindPin(link.StartPinID);
                        MaterialPin* p2 = FindPin(link.EndPinID);
                        return (p1 && p1->NodeID == deletedNodeId) || (p2 && p2->NodeID == deletedNodeId);
                    }), m_Links.end());
                m_Nodes.erase(std::remove_if(m_Nodes.begin(), m_Nodes.end(),
                    [deletedNodeId](const MaterialNode& n) { return n.ID == deletedNodeId; }), m_Nodes.end());
                hasDeleted = true;
            }
        }
        if (hasDeleted) CompilePreviewShader();
    }
    ed::EndDelete();

    // 3. RACCOURCIS SOURIS (Alt + Clic pour couper)
    ed::PinId hoveredPin = ed::GetHoveredPin();
    if (hoveredPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt) {
        size_t beforeSize = m_Links.size();
        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
            [hoveredPin](const MaterialLink& link) { return link.StartPinID == hoveredPin || link.EndPinID == hoveredPin; }), m_Links.end());
        if (m_Links.size() < beforeSize) CompilePreviewShader();
    }
}

void MaterialEditorPanel::DrawContextMenus() {
    ed::Suspend();

    // Déclenchement naturel via Clic Droit
    if (ed::ShowBackgroundContextMenu()) {
        m_NewNodeLinkPinId = 0;
        m_ContextPopupPos = ed::ScreenToCanvas(ImGui::GetMousePos());
        ImGui::OpenPopup("NodeContextMenu");
    }
    // Déclenchement forcé via un tirage de câble dans le vide
    if (m_RequestNodeMenu) {
        ImGui::OpenPopup("NodeContextMenu");
        m_RequestNodeMenu = false;
    }

    // MENU 1 : AJOUT DE NOEUD
    if (ImGui::BeginPopup("NodeContextMenu")) {
        auto popupPos = ImGui::GetMousePosOnOpeningCurrentPopup();
        m_ContextPopupPos = ed::ScreenToCanvas(popupPos);

        std::map<std::string, std::vector<std::shared_ptr<IMaterialNodeDef>>> categorizedNodes;
        for (const auto& [name, def] : MaterialNodeRegistry::GetRegistry()) categorizedNodes[def->GetCategory()].push_back(def);

        for (const auto& [category, nodes] : categorizedNodes) {
            if (ImGui::BeginMenu(category.c_str())) {
                for (const auto& def : nodes) {
                    if (ImGui::MenuItem(def->GetName().c_str())) {
                        MaterialNode newNode;
                        if (MaterialNodeRegistry::CreateNode(def->GetName(), m_NextId, newNode)) {
                            ed::SetNodePosition(newNode.ID, m_ContextPopupPos);
                            m_Nodes.push_back(newNode);

                            // Auto-Connexion si on a tiré un câble !
                            if (m_NewNodeLinkPinId.Get() != 0) {
                                MaterialPin* startPin = FindPin(m_NewNodeLinkPinId);
                                if (startPin) {
                                    MaterialPin* targetPin = nullptr;
                                    auto& pinsList = (startPin->Kind == ed::PinKind::Output) ? m_Nodes.back().Inputs : m_Nodes.back().Outputs;
                                    for (auto& pin : pinsList) if (pin.Type == startPin->Type) { targetPin = &pin; break; }
                                    if (!targetPin && !pinsList.empty()) targetPin = &pinsList[0];

                                    if (targetPin) m_Links.push_back({ ed::LinkId(m_NextId++), startPin->ID, targetPin->ID });
                                }
                                m_NewNodeLinkPinId = 0;
                            }
                            CompilePreviewShader();
                        }
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    } else {
        m_NewNodeLinkPinId = 0;
    }

    // MENU 2 : PROPRIETES D'UN NOEUD (Clic Droit sur un Nœud existant)
    ed::NodeId contextNodeId = 0;
    if (ed::ShowNodeContextMenu(&contextNodeId)) {
        m_ContextNodeId = contextNodeId;
        ImGui::OpenPopup("NodePropertiesPopup");
    }

    if (ImGui::BeginPopup("NodePropertiesPopup")) {
        MaterialNode* node = FindNode(m_ContextNodeId);

        if (node && (node->Name == "Float" || node->Name == "Color" || node->Name == "Texture2D" || node->Name == "StaticSwitchParameter" || node->Name == "Comment")) {

            if (node->Name == "Comment") {
                char buf[256];
                strncpy(buf, node->CommentText.c_str(), sizeof(buf));
                if (ImGui::InputText("Text", buf, sizeof(buf))) node->CommentText = buf;
                ImGui::ColorEdit4("Color", &node->ColorValue[0]);
            }
            else {
                if (node->Name != "StaticSwitchParameter") {
                    if (ImGui::Checkbox("Is Parameter", &node->IsParameter)) {
                        if (node->IsParameter && node->ParameterName.empty()) node->ParameterName = "Param_" + std::to_string((int)node->ID.Get());
                        CompilePreviewShader();
                    }
                }

                if (node->IsParameter) {
                    char buf[128];
                    strncpy(buf, node->ParameterName.c_str(), sizeof(buf));
                    if (ImGui::InputText("Name", buf, sizeof(buf))) { node->ParameterName = buf; CompilePreviewShader(); }

                    char catBuf[128];
                    strncpy(catBuf, node->ParameterCategory.c_str(), sizeof(catBuf));
                    if (ImGui::InputText("Category", catBuf, sizeof(catBuf))) node->ParameterCategory = catBuf;
                }

                if (node->Name == "StaticSwitchParameter") {
                    if (ImGui::Checkbox("Default Value", &node->BoolValue)) CompilePreviewShader();
                }
            }
        } else {
            ImGui::TextDisabled("No properties available");
        }
        ImGui::EndPopup();
    }

    ed::Resume();
}

MaterialPin* MaterialEditorPanel::FindPin(ed::PinId id) {
    if (!id) return nullptr;
    for (auto& node : m_Nodes) {
        for (auto& pin : node.Inputs) {
            if (pin.ID == id) return &pin;
        }
        for (auto& pin : node.Outputs) {
            if (pin.ID == id) return &pin;
        }
    }
    return nullptr;
}

void MaterialEditorPanel::Save(const std::filesystem::path& path) {
    ed::SetCurrentEditor(m_Context);

    nlohmann::json data;
    data["Type"] = "MaterialGraph";
    data["NextID"] = m_NextId;
    data["GeneratedGLSL"] = CompileMaterial();

    // =========================================================================
    // RÉSOLUTION INTELLIGENTE DU PBR (Style Unreal Engine)
    // =========================================================================
    MaterialNode* baseNode = nullptr;
    for (auto& n : m_Nodes) { if (n.Name == "Base Material") { baseNode = &n; break; } }

    if (baseNode) {
        // 1. Traceur de Texture Récursif
        std::function<std::string(ed::PinId)> getConnectedTexture = [&](ed::PinId pinId) -> std::string {
            for (auto& link : m_Links) {
                if (link.EndPinID == pinId) {
                    MaterialPin* outPin = FindPin(link.StartPinID);
                    if (outPin) {
                        MaterialNode* srcNode = FindNode(outPin->NodeID);
                        if (!srcNode) continue;

                        if (srcNode->Name == "Texture2D") return srcNode->TexturePath;

                        // Si c'est un Switch, on bifurque selon son état ! (0 = True, 1 = False)
                        if (srcNode->Name == "StaticSwitchParameter" || srcNode->Name == "Static Switch Parameter") {
                            if (srcNode->Inputs.size() >= 2) {
                                int activeIdx = srcNode->BoolValue ? 0 : 1;
                                return getConnectedTexture(srcNode->Inputs[activeIdx].ID);
                            }
                        }

                        // Si c'est un autre noeud intermédiaire (ex: Multiply), on fouille ses entrées
                        for (auto& inPin : srcNode->Inputs) {
                            std::string res = getConnectedTexture(inPin.ID);
                            if (!res.empty()) return res;
                        }
                    }
                }
            }
            return "";
        };

        // 2. Traceur de Float Récursif
        std::function<float(ed::PinId, float)> getConnectedFloat = [&](ed::PinId pinId, float def) -> float {
            for (auto& link : m_Links) {
                if (link.EndPinID == pinId) {
                    MaterialPin* outPin = FindPin(link.StartPinID);
                    if (outPin) {
                        MaterialNode* srcNode = FindNode(outPin->NodeID);
                        if (!srcNode) continue;

                        if (srcNode->Name == "Float") return srcNode->FloatValue;

                        if (srcNode->Name == "StaticSwitchParameter" || srcNode->Name == "Static Switch Parameter") {
                            if (srcNode->Inputs.size() >= 2) {
                                int activeIdx = srcNode->BoolValue ? 0 : 1;
                                return getConnectedFloat(srcNode->Inputs[activeIdx].ID, def);
                            }
                        }

                        for (auto& inPin : srcNode->Inputs) {
                            float res = getConnectedFloat(inPin.ID, -9999.0f);
                            if (res != -9999.0f) return res;
                        }
                    }
                }
            }
            return def;
        };

        // 3. Traceur de Couleur Récursif
        std::function<nlohmann::json(ed::PinId)> getConnectedColor = [&](ed::PinId pinId) -> nlohmann::json {
            for (auto& link : m_Links) {
                if (link.EndPinID == pinId) {
                    MaterialPin* outPin = FindPin(link.StartPinID);
                    if (outPin) {
                        MaterialNode* srcNode = FindNode(outPin->NodeID);
                        if (!srcNode) continue;

                        if (srcNode->Name == "Color") return {srcNode->ColorValue.r, srcNode->ColorValue.g, srcNode->ColorValue.b, srcNode->ColorValue.a};

                        if (srcNode->Name == "StaticSwitchParameter" || srcNode->Name == "Static Switch Parameter") {
                            if (srcNode->Inputs.size() >= 2) {
                                int activeIdx = srcNode->BoolValue ? 0 : 1;
                                return getConnectedColor(srcNode->Inputs[activeIdx].ID);
                            }
                        }

                        for (auto& inPin : srcNode->Inputs) {
                            nlohmann::json res = getConnectedColor(inPin.ID);
                            if (!res.empty()) return res;
                        }
                    }
                }
            }
            return nlohmann::json();
        };

        // On sauvegarde tout en se basant sur les ID des Pins du Base Material
        if (baseNode->Inputs.size() >= 6) {
            data["PBR_Albedo"] = getConnectedTexture(baseNode->Inputs[0].ID);
            data["PBR_Normal"] = getConnectedTexture(baseNode->Inputs[1].ID);
            data["PBR_Metallic"] = getConnectedTexture(baseNode->Inputs[2].ID);
            data["PBR_Roughness"] = getConnectedTexture(baseNode->Inputs[3].ID);
            data["PBR_AO"] = getConnectedTexture(baseNode->Inputs[5].ID); // Specular est en index 4

            nlohmann::json cVal = getConnectedColor(baseNode->Inputs[0].ID);
            data["PBR_ColorVal"] = cVal.empty() ? nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f}) : cVal;

            data["PBR_MetallicVal"] = getConnectedFloat(baseNode->Inputs[2].ID, 0.0f);
            data["PBR_RoughnessVal"] = getConnectedFloat(baseNode->Inputs[3].ID, 0.5f);
            data["PBR_AOVal"] = getConnectedFloat(baseNode->Inputs[5].ID, 1.0f);
        }
    }

    // --- SAUVEGARDE DES NOEUDS ---
    auto& nodesOut = data["Nodes"];
    for (auto& node : m_Nodes) {
        nlohmann::json nodeJson;
        nodeJson["ID"] = (int)node.ID.Get();
        nodeJson["Name"] = node.Name;

        ImVec2 pos = ed::GetNodePosition(node.ID);
        nodeJson["Position"] = { pos.x, pos.y };

        nodeJson["FloatValue"] = node.FloatValue;
        nodeJson["ColorValue"] = { node.ColorValue.r, node.ColorValue.g, node.ColorValue.b, node.ColorValue.a };
        nodeJson["BoolValue"] = node.BoolValue;
        nodeJson["TexturePath"] = node.TexturePath;

        nodeJson["IsParameter"] = node.IsParameter;
        nodeJson["ParameterName"] = node.ParameterName;
        nodeJson["ParameterCategory"] = node.ParameterCategory;

        nodeJson["Size"] = { node.Size.x, node.Size.y };
        nodeJson["CommentText"] = node.CommentText;

        for (auto& pin : node.Inputs) {
            nodeJson["Inputs"].push_back({
                {"ID", (int)pin.ID.Get()},
                {"Name", pin.Name},
                {"Type", (int)pin.Type},
                {"FloatValue", pin.FloatValue},
                {"Vec2Value", {pin.Vec2Value.x, pin.Vec2Value.y}},
                {"Vec3Value", {pin.Vec3Value.r, pin.Vec3Value.g, pin.Vec3Value.b}}
            });
        }
        for (auto& pin : node.Outputs) {
            nodeJson["Outputs"].push_back({
                {"ID", (int)pin.ID.Get()},
                {"Name", pin.Name},
                {"Type", (int)pin.Type}
            });
        }
        nodesOut.push_back(nodeJson);
    }

    // --- SAUVEGARDE DES CÂBLES ---
    auto& linksOut = data["Links"];
    for (auto& link : m_Links) {
        linksOut.push_back({
            {"ID", (int)link.ID.Get()},
            {"StartPinID", (int)link.StartPinID.Get()},
            {"EndPinID", (int)link.EndPinID.Get()}
        });
    }

    std::ofstream file(path);
    file << data.dump(4); // Formatage propre (4 espaces)

    ed::SetCurrentEditor(nullptr);
    CompilePreviewShader(); // Mise à jour du viewport après sauvegarde

    // --- NOUVEAU : On avertit le reste du moteur ! ---
    if (OnMaterialSavedCallback) {
        OnMaterialSavedCallback(path);
    }
}

void MaterialEditorPanel::Load(const std::filesystem::path& path) {
    m_CurrentPath = path;

    std::ifstream file(path);
    if (!file.is_open()) return;

    nlohmann::json data;
    try { file >> data; } catch(...) { return; }

    ed::SetCurrentEditor(m_Context);
    m_Nodes.clear();
    m_Links.clear();

    if (!data.contains("Nodes") || data["Nodes"].empty()) { // <--- Ajout du empty()
        BuildDefaultNodes();
        ed::SetCurrentEditor(nullptr);
        CompilePreviewShader(); // On n'oublie pas de compiler la sphère par défaut !
        return;
    }

    m_NextId = data.value("NextID", 1);

    // L'Airbag Mémoire (Empêche les pointeurs de mourir)
    m_Nodes.reserve(data["Nodes"].size());

    // --- CHARGEMENT DES NOEUDS (Méthode AAA) ---
    for (auto& nodeJson : data["Nodes"]) {
        std::string nodeName = nodeJson["Name"].get<std::string>();

        // 1. On demande au registre de créer le noeud PARFAIT avec la structure C++ actuelle !
        MaterialNode node;

        // --- LE FIX EST LÀ : Bypass pour les noeuds purement visuels (Commentaires) ---
        if (nodeName == "Comment") {
            node.Name = "Comment";
        }
        // 1. On demande au registre pour les VRAIS noeuds de shader
        else if (!MaterialNodeRegistry::CreateNode(nodeName, m_NextId, node)) {
            std::cout << "[MaterialEditor] Warning: Noeud '" << nodeName << "' obsolete ignore." << std::endl;
            continue;
        }

        // 2. On écrase ses propriétés avec celles de la sauvegarde
        node.ID = ed::NodeId(nodeJson["ID"].get<int>());

        for (auto& pin : node.Inputs) pin.NodeID = node.ID;
        for (auto& pin : node.Outputs) pin.NodeID = node.ID;

        if (nodeJson.contains("FloatValue")) node.FloatValue = nodeJson["FloatValue"].get<float>();
        if (nodeJson.contains("BoolValue")) node.BoolValue = nodeJson["BoolValue"].get<bool>();
        if (nodeJson.contains("ColorValue")) {
            node.ColorValue = { nodeJson["ColorValue"][0], nodeJson["ColorValue"][1], nodeJson["ColorValue"][2], nodeJson["ColorValue"][3] };
        }
        if (nodeJson.contains("TexturePath")) {
            node.TexturePath = nodeJson["TexturePath"].get<std::string>();
            if (!node.TexturePath.empty()) node.TextureID = TextureLoader::LoadTexture(node.TexturePath.c_str());
        }
        if (nodeJson.contains("Size")) node.Size = { nodeJson["Size"][0].get<float>(), nodeJson["Size"][1].get<float>() };
        if (nodeJson.contains("CommentText")) node.CommentText = nodeJson["CommentText"].get<std::string>();

        // ==============================================================
        // LE VRAI FIX EST LÀ : On lit les paramètres au niveau du nœud !
        // ==============================================================
        if (nodeJson.contains("IsParameter")) node.IsParameter = nodeJson["IsParameter"].get<bool>();
        if (nodeJson.contains("ParameterName")) node.ParameterName = nodeJson["ParameterName"].get<std::string>();
        if (nodeJson.contains("ParameterCategory")) node.ParameterCategory = nodeJson["ParameterCategory"].get<std::string>();

        // 3. On restaure les IDs des Pins en les cherchant PAR NOM (Rétro-compatibilité absolue)
        if (nodeJson.contains("Inputs")) {
            for (auto& pinJson : nodeJson["Inputs"]) {
                std::string pinName = pinJson["Name"].get<std::string>();
                for (auto& pin : node.Inputs) {
                    if (pin.Name == pinName) {
                        pin.ID = ed::PinId(pinJson["ID"].get<int>());

                        // ATTENTION : On a bien supprimé les IsParameter qui trainaient ici !

                        if (pinJson.contains("FloatValue")) pin.FloatValue = pinJson["FloatValue"].get<float>();
                        if (pinJson.contains("Vec2Value")) pin.Vec2Value = { pinJson["Vec2Value"][0], pinJson["Vec2Value"][1] };
                        if (pinJson.contains("Vec3Value")) pin.Vec3Value = { pinJson["Vec3Value"][0], pinJson["Vec3Value"][1], pinJson["Vec3Value"][2] };
                        break;
                    }
                }
            }
        }
        if (nodeJson.contains("Outputs")) {
            for (auto& pinJson : nodeJson["Outputs"]) {
                std::string pinName = pinJson["Name"].get<std::string>();
                for (auto& pin : node.Outputs) {
                    if (pin.Name == pinName) {
                        pin.ID = ed::PinId(pinJson["ID"].get<int>());
                        break;
                    }
                }
            }
        }

        m_Nodes.push_back(node);

        if (nodeJson.contains("Position")) {
            ed::SetNodePosition(node.ID, ImVec2(nodeJson["Position"][0].get<float>(), nodeJson["Position"][1].get<float>()));
        }
    }

    // --- CHARGEMENT DES CÂBLES ---
    if (data.contains("Links")) {
        for (auto& linkJson : data["Links"]) {
            ed::PinId startId = ed::PinId(linkJson["StartPinID"].get<int>());
            ed::PinId endId = ed::PinId(linkJson["EndPinID"].get<int>());

            if (FindPin(startId) && FindPin(endId)) {
                MaterialLink link;
                link.ID = ed::LinkId(linkJson["ID"].get<int>());
                link.StartPinID = startId;
                link.EndPinID = endId;
                m_Links.push_back(link);
            }
        }
    }

    ed::SetCurrentEditor(nullptr);
    m_FirstFrame = false;

    UpdateWildcardPins();

    CompilePreviewShader();
}

// --- FONCTION UTILITAIRE ---
MaterialNode* MaterialEditorPanel::FindNode(ed::NodeId id) {
    for (auto& node : m_Nodes) {
        if (node.ID == id) return &node;
    }
    return nullptr;
}

// --- LE CHEF D'ORCHESTRE ---
std::string MaterialEditorPanel::CompileMaterial() {
    MaterialNode* rootNode = nullptr;
    for (auto& node : m_Nodes) {
        if (node.Name == "Base Material") { rootNode = &node; break; }
    }
    if (!rootNode) return "";

    std::stringstream shaderCode;
    shaderCode << "#version 460 core\n\n";

    // --- VARIABLES DE SORTIE ---
    shaderCode << "layout(location = 0) out vec4 FragColor;\n";
    shaderCode << "layout(location = 1) out int EntityID;\n\n";

    // --- VARIABLES D'ENTRÉE ---
    shaderCode << "in vec3 vFragPos;\n";
    shaderCode << "in vec3 vNormal;\n";
    shaderCode << "in vec2 vTexCoords;\n";
    shaderCode << "in vec3 vTangent;\n";
    shaderCode << "in float vViewDepth;\n\n";

    // --- UNIFORMS STANDARDS ---
    shaderCode << "uniform int uEntityID;\n";
    shaderCode << "uniform int uRenderMode;\n\n";

    // --- UNIFORMS CSM (NOUVEAU) ---
    shaderCode << "uniform sampler2DArray uShadowMap;\n";
    shaderCode << "uniform mat4 uLightSpaceMatrices[3];\n";
    shaderCode << "uniform float uCascadeDistances[3];\n\n";

    // --- UNIFORMS IBL ---
    shaderCode << "uniform sampler2D uDDGIIrradiance;\n";
    shaderCode << "uniform int uDDGIProbeCount[3];\n"; // Transféré via SetInt3

    shaderCode << "uniform float uDDGIStartX, uDDGIStartY, uDDGIStartZ;\n";
    shaderCode << "uniform float uDDGISpaceX, uDDGISpaceY, uDDGISpaceZ;\n";

    shaderCode << "uniform float u_SkyboxIntensity = 0.5;\n";
    shaderCode << "uniform float u_SkyboxRotation = 0.0;\n\n";

    shaderCode << "uniform samplerCube uPrefilterMap;\n";
    shaderCode << "uniform sampler2D uBRDFLUT;\n";

    // --- PARAMÈTRES (INSTANCES) ET TEXTURES ---
    for (auto& node : m_Nodes) {
        if (node.IsParameter) {
            if (node.Name == "Float") {
                // On injecte la valeur par défaut ! (ex: uniform float u_Tiling = 1.0;)
                shaderCode << "uniform float u_" << node.ParameterName << " = " << node.FloatValue << ";\n";
            }
            else if (node.Name == "Color") {
                // Pareil pour les couleurs
                shaderCode << "uniform vec4 u_" << node.ParameterName << " = vec4("
                           << node.ColorValue.r << ", " << node.ColorValue.g << ", "
                           << node.ColorValue.b << ", " << node.ColorValue.a << ");\n";
            }
            else if (node.Name == "Texture2D") {
                shaderCode << "uniform sampler2D u_" << node.ParameterName << ";\n";
            }
        } else if (node.Name == "Texture2D" && !node.TexturePath.empty()) {
            shaderCode << "uniform sampler2D u_Tex_" << node.ID.Get() << ";\n";
        }
    }

    // --- UNIFORMS PBR ---
    shaderCode << "uniform vec3 uViewPos;\n";
    shaderCode << "uniform vec3 uLightDir;\n"; // <-- Remplacé uLightPos par uLightDir !
    shaderCode << "uniform vec3 uLightColor;\n\n";

    // --- FONCTIONS MATHÉMATIQUES PBR & OMBRES ---
    // (J'utilise un raw string R"()" pour ne pas avoir à échapper chaque ligne, c'est bien plus propre)
    shaderCode << R"(
const float PI = 3.14159265359;

vec3 ACESFilm(vec3 x) {
    float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// --- MATHÉMATIQUES DDGI ---
vec2 OctEncode(vec3 v) {
    float l = abs(v.x) + abs(v.y) + abs(v.z);
    vec2 oct = v.xy / l;
    if (v.z < 0.0) {
        vec2 signOct = vec2(oct.x >= 0.0 ? 1.0 : -1.0, oct.y >= 0.0 ? 1.0 : -1.0);
        oct = (1.0 - abs(oct.yx)) * signOct;
    }
    return oct * 0.5 + 0.5;
}

vec3 SampleDDGIIrradiance(vec3 worldPos, vec3 normal) {
    vec3 startPos = vec3(uDDGIStartX, uDDGIStartY, uDDGIStartZ);
    vec3 spacing = vec3(uDDGISpaceX, uDDGISpaceY, uDDGISpaceZ);
    ivec3 probeCount = ivec3(uDDGIProbeCount[0], uDDGIProbeCount[1], uDDGIProbeCount[2]);

    // 1. Position exacte dans la grille
    vec3 gridPos = (worldPos - startPos) / spacing;
    ivec3 baseProbeCoords = ivec3(floor(gridPos));
    vec3 alpha = fract(gridPos);

    vec3 sumIrradiance = vec3(0.0);
    float sumWeight = 0.0;
    int probesPerRow = probeCount.x * probeCount.y;
    vec2 texSize = vec2(float(probesPerRow * 8), float(probeCount.z * 8));

    // 2. Interpolation des 8 sondes autour de l'objet
    for (int i = 0; i < 8; ++i) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 probeCoords = clamp(baseProbeCoords + offset, ivec3(0), probeCount - ivec3(1));

        int probeIndex = probeCoords.x + probeCoords.y * probeCount.x + probeCoords.z * probesPerRow;
        int gridX = probeIndex % probesPerRow;
        int gridY = probeIndex / probesPerRow;

        vec2 octUV = OctEncode(normal);
        vec2 pixelPos = vec2(gridX * 8.0, gridY * 8.0) + (octUV * 8.0);

        vec3 probeIrradiance = texture(uDDGIIrradiance, pixelPos / texSize).rgb;

        vec3 trilinear = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = trilinear.x * trilinear.y * trilinear.z;

        sumIrradiance += probeIrradiance * weight;
        sumWeight += weight;
    }
    return sumIrradiance / max(sumWeight, 0.0001);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness; float a2 = a*a; float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH*NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0); float k = (r*r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// --- LE FIX DE LA SURBRILLANCE (HALO LUMINEUX) ---
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 getNormalFromMap(vec3 normalMapColor, vec3 fragPos, vec3 normal, vec2 uv) {
    vec3 tangentNormal = normalize(normalMapColor * 2.0 - 1.0);
    vec3 N = normalize(normal);
    vec3 T;

    // Utiliser la tangente par vertex si disponible
    if (length(vTangent) > 0.001) {
        T = normalize(vTangent);
    } else {
        // Fallback screen-space pour les meshes sans tangentes
        vec3 Q1 = dFdx(fragPos);
        vec3 Q2 = dFdy(fragPos);
        vec2 st1 = dFdx(uv);
        vec2 st2 = dFdy(uv);
        float det = (st1.x * st2.y - st2.x * st1.y);
        if (abs(det) > 0.0001) {
            T = normalize((Q1 * st2.y - Q2 * st1.y) / det);
        } else {
            T = cross(abs(N.z) > 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0), N);
        }
    }

    // Orthogonalisation de Gram-Schmidt
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

// ========================================================
// --- SYSTEME D'OMBRE PCSS (Nouveau) ---
// ========================================================
const float GOLDEN_ANGLE = 2.39996323;

vec2 VogelDiskSample(int i, int numSamples, float noiseAngle) {
    float r = sqrt(float(i) + 0.5) / sqrt(float(numSamples));
    float theta = float(i) * GOLDEN_ANGLE + noiseAngle;
    return vec2(r * cos(theta), r * sin(theta));
}

float InterleavedGradientNoise(vec2 position_screen) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position_screen, magic.xy)));
}

vec2 ShadowCalculation(vec3 fragPosWorld, vec3 normal, vec3 lightDir) {
    int layer = -1;
    for (int i = 0; i < 3; ++i) {
        if (vViewDepth < uCascadeDistances[i]) { layer = i; break; }
    }
    if (layer == -1) layer = 2;

    float normalBiasOffset = 2.0;
    if (layer == 1) normalBiasOffset = 6.0;
    if (layer == 2) normalBiasOffset = 20.0;

    vec3 biasedFragPos = fragPosWorld + normal * normalBiasOffset;
    vec4 fragPosLightSpace = uLightSpaceMatrices[layer] * vec4(biasedFragPos, 1.0);

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if(projCoords.z > 1.0) return vec2(0.0, layer);

    float currentDepth = projCoords.z;
    float bias = max(0.0005 * (1.0 - dot(normal, lightDir)), 0.00005);

    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    float angle = noise * 6.28318530718;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));

    // PCSS - ÉTAPE 1 : Blocker Search
    int blockers = 0;
    float avgBlockerDepth = 0.0;
    float searchRadius = 4.0;
    if (layer == 1) searchRadius = 2.0;
    if (layer == 2) searchRadius = 1.0;

    int blockerSamples = 16;
    for(int i = 0; i < blockerSamples; i++) {
        vec2 offset = VogelDiskSample(i, blockerSamples, angle) * searchRadius * texelSize;
        float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + offset, float(layer))).r;
        if (pcfDepth < currentDepth - bias) {
            blockers++;
            avgBlockerDepth += pcfDepth;
        }
    }

    if (blockers == 0) return vec2(0.0, layer);
    avgBlockerDepth /= float(blockers);

    // PCSS - ÉTAPE 2 : Penumbra Estimation
    float distanceToBlocker = currentDepth - avgBlockerDepth;
    float sunSize = 150.0;
    float penumbraRadius = distanceToBlocker * sunSize;

    float maxBlur = 6.0;
    if (layer == 1) maxBlur = 4.0;
    if (layer == 2) maxBlur = 2.0;
    float filterRadiusUV = clamp(penumbraRadius, 1.0, maxBlur);

    // PCSS - ÉTAPE 3 : Variable PCF
    float shadow = 0.0;
    int pcfSamples = 32;
    for(int i = 0; i < pcfSamples; i++) {
        vec2 offset = VogelDiskSample(i, pcfSamples, angle) * filterRadiusUV * texelSize;
        float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + offset, float(layer))).r;
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }
    shadow /= float(pcfSamples);

    return vec2(shadow, layer);
}
)";

    // ========================================================
    // --- EVALUATION DU GRAPHE ---
    // ========================================================
    std::unordered_set<int> visitedNodes;
    std::stringstream bodyBuilder;

    // LE FIX ABSOLU DES PINS DÉBRANCHÉS
    auto getRootPin = [&](int index, const std::string& fallback) {
        if (index >= rootNode->Inputs.size()) return fallback;
        bool hasLink = false;
        for (auto& l : m_Links) {
            if (l.EndPinID == rootNode->Inputs[index].ID) { hasLink = true; break; }
        }
        if (hasLink) return EvaluatePinGLSL(rootNode->Inputs[index].ID, visitedNodes, bodyBuilder);
        return fallback;
    };

    std::string v_baseColor = getRootPin(0, "vec3(1.0)");

    // Si la normale est débranchée, on force un bleu flat parfait (0.5, 0.5, 1.0) !
    std::string v_normal    = getRootPin(1, "vec3(0.5, 0.5, 1.0)");

    std::string v_metallic  = getRootPin(2, "0.0");
    std::string v_roughness = getRootPin(3, "0.5");
    std::string v_specular  = getRootPin(4, "0.5");
    std::string v_ao        = getRootPin(5, "1.0");

    // --- LE FIX DE COMPILATION : On remet le détecteur disparu ! ---
    bool normalConnected = false;
    if (rootNode->Inputs.size() > 1) {
        for (auto& link : m_Links) {
            if (link.EndPinID == rootNode->Inputs[1].ID) { normalConnected = true; break; }
        }
    }

    // ========================================================
    // --- MAIN ---
    // ========================================================
    shaderCode << "void main() {\n";

    shaderCode << bodyBuilder.str();

    shaderCode << "    vec3 albedo = pow(vec3(" << v_baseColor << "), vec3(2.2));\n";
    shaderCode << "    float metallic = float(" << v_metallic << ");\n";
    shaderCode << "    float roughness = clamp(float(" << v_roughness << "), 0.05, 1.0);\n";
    shaderCode << "    float specularValue = clamp(float(" << v_specular << "), 0.0, 1.0);\n";
    shaderCode << "    float ao = float(" << v_ao << ");\n\n";

    if (normalConnected) {
        shaderCode << "    vec3 N = getNormalFromMap(vec3(" << v_normal << "), vFragPos, vNormal, vTexCoords);\n";
    } else {
        shaderCode << "    vec3 N = normalize(vNormal);\n";
    }
    shaderCode << "    if (uRenderMode == 1 || uRenderMode == 2) {\n";
    shaderCode << "        FragColor = vec4(albedo, 1.0);\n";
    shaderCode << "        EntityID = uEntityID;\n";
    shaderCode << "        return;\n";
    shaderCode << "    }\n";

    // --- LE BOUCLIER ANTI-DIVISION PAR ZÉRO ---
    shaderCode << "    roughness = clamp(roughness, 0.05, 1.0);\n";
    shaderCode << "    ao = clamp(ao, 0.05, 1.0);\n\n";

    // J'ai supprimé l'AA Spéculaire qui détruisait tes Normal Maps !

    // --- LE CÂBLAGE FINAL OMBRE + PBR ---
    shaderCode << R"(
    roughness = clamp(roughness, 0.05, 1.0);
    ao = clamp(ao, 0.01, 1.0);

    vec3 V = normalize(uViewPos - vFragPos);
    vec3 L = normalize(-uLightDir);
    vec3 H = normalize(V + L + vec3(0.000001));

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 radiance = uLightColor;

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    specular = clamp(specular, vec3(0.0), vec3(10.0));

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    #ifndef IS_PREVIEW_VIEWPORT
        vec2 shadowData = ShadowCalculation(vFragPos, N, L);
        float shadow = shadowData.x;
    #else
        float shadow = 0.0;
    #endif

    // ========================================================
    // --- APPLICATION RÉALISTE DU CIEL (IBL COMPLET) ---
    // ========================================================
    float skyC = cos(u_SkyboxRotation); float skyS = sin(u_SkyboxRotation);

    // --- LE FIX DES MESHES BLANCS (Séparation Fond / Lumière) ---
    // On bride artificiellement la lumière du ciel HDR pour qu'elle agisse
    // comme une ombre douce de jeu vidéo, sans éclipser le Soleil !
    float iblExposure = 0.02;

    // 1. DIFFUSE DDGI
    vec3 irradiance = SampleDDGIIrradiance(vFragPos, N) * iblExposure;

    // 2. SPECULAR IBL (Les reflets dynamiques !)
    vec3 R = reflect(-V, N);
    vec3 rotR = vec3(R.x * skyC - R.y * skyS, R.x * skyS + R.y * skyC, R.z);

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 iblReflect = vec3(rotR.x, rotR.z, -rotR.y);
    vec3 prefilteredColor = textureLod(uPrefilterMap, iblReflect, roughness * MAX_REFLECTION_LOD).rgb * u_SkyboxIntensity * iblExposure;

    vec2 envBRDF = texture(uBRDFLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;

    vec3 F_ambient = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD_ambient = 1.0 - F_ambient;
    kD_ambient *= 1.0 - metallic;

    vec3 diffuseIBL = irradiance * albedo;
    vec3 specularIBL = prefilteredColor * (F_ambient * envBRDF.x + envBRDF.y);

    vec3 ambient = (kD_ambient * diffuseIBL + specularIBL) * ao;

    // ========================================================
    vec3 color = ambient + (1.0 - shadow) * Lo;

    // --- LE BOUCLIER ANTI-INVISIBILITÉ LINUX ---
    if (isnan(color.x) || isnan(color.y) || isnan(color.z) || isinf(color.x)) {
        color = vec3(0.0);
    }

    // Tonemapping ACES
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
    EntityID = uEntityID;
}
)";

    return shaderCode.str();
}


// --- L'ALGORITHME MAGIQUE (Récursif) ---
std::string MaterialEditorPanel::EvaluatePinGLSL(ed::PinId inputPinId, std::unordered_set<int>& visited, std::stringstream& bodyBuilder) {
    MaterialPin* myInputPin = FindPin(inputPinId);
    if (!myInputPin) return "0.0";

    PinType expectedType = myInputPin->Type;

    // 1. Est-ce branché ?
    MaterialLink* connectedLink = nullptr;
    for (auto& link : m_Links) {
        if (link.EndPinID == inputPinId) { connectedLink = &link; break; }
    }

    // 2. Si rien n'est branché, on renvoie la valeur statique (Interface)
    if (!connectedLink) {
        std::stringstream ss;
        if (expectedType == PinType::Float) { ss << myInputPin->FloatValue; return ss.str(); }
        if (expectedType == PinType::Vec2)  { ss << "vec2(" << myInputPin->Vec2Value.x << ", " << myInputPin->Vec2Value.y << ")"; return ss.str(); }
        if (expectedType == PinType::Vec3)  { ss << "vec3(" << myInputPin->Vec3Value.r << ", " << myInputPin->Vec3Value.g << ", " << myInputPin->Vec3Value.b << ")"; return ss.str(); }
        if (expectedType == PinType::Vec4)  return "vec4(0.0)";
        return "0.0";
    }

    MaterialPin* outputPin = FindPin(connectedLink->StartPinID);
    if (!outputPin) return "0.0";
    MaterialNode* sourceNode = FindNode(outputPin->NodeID);
    if (!sourceNode) return "0.0";

    // --- BYPASS REROUTE ---
    if (sourceNode->Name == "Reroute") {
        std::string result = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
        return CastGLSL(result, outputPin->Type, expectedType);
    }

    int sourceId = (int)sourceNode->ID.Get();
    std::string baseVar = "val_" + std::to_string(sourceId);

    // 3. Génération du GLSL du noeud Source (Si pas encore fait)
    if (visited.find(sourceId) == visited.end()) {
        visited.insert(sourceId);
        bodyBuilder << "    // Noeud: " << sourceNode->Name << " (ID: " << sourceId << ")\n";

        // Le Lambda d'évaluation fourni au nœud !
        auto evalInput = [&](int index, const std::string& fallback) -> std::string {
            if (index < 0 || index >= sourceNode->Inputs.size()) return fallback;
            ed::PinId inPinId = sourceNode->Inputs[index].ID;

            bool hasLink = false;
            for (auto& l : m_Links) if (l.EndPinID == inPinId) { hasLink = true; break; }

            if (hasLink) {
                return EvaluatePinGLSL(inPinId, visited, bodyBuilder); // Récursivité
            } else if (!fallback.empty()) {
                return fallback; // Utilisation du fallback magique (ex: vTexCoords)
            } else {
                // Lecture de la constante locale du Pin (Si pas de fallback)
                MaterialPin* p = FindPin(inPinId);
                if (p) {
                    std::stringstream ss;
                    if (p->Type == PinType::Float) { ss << p->FloatValue; return ss.str(); }
                    if (p->Type == PinType::Vec2)  { ss << "vec2(" << p->Vec2Value.x << ", " << p->Vec2Value.y << ")"; return ss.str(); }
                    if (p->Type == PinType::Vec3)  { ss << "vec3(" << p->Vec3Value.r << ", " << p->Vec3Value.g << ", " << p->Vec3Value.b << ")"; return ss.str(); }
                    if (p->Type == PinType::Vec4)  return "vec4(0.0)";
                }
                return "0.0";
            }
        };

        // On délègue l'écriture au Registre !
        MaterialNodeRegistry::GenerateNodeGLSL(*sourceNode, bodyBuilder, evalInput);
    }

    // 4. Extraction des composants (.r, .g, .rgb)
    std::string providedExpr = baseVar;

    // --- LE FIX POUR LE BREAK VEC3 EST ICI ---
    if (sourceNode->Name == "Color" || sourceNode->Name == "Texture2D" || sourceNode->Name == "Break Vec3") {
        if (outputPin->Name == "RGB") providedExpr += ".rgb";
        else if (outputPin->Name == "R" || outputPin->Name == "X (R)") providedExpr += ".x";
        else if (outputPin->Name == "G" || outputPin->Name == "Y (G)") providedExpr += ".y";
        else if (outputPin->Name == "B" || outputPin->Name == "Z (B)") providedExpr += ".z";
        else if (outputPin->Name == "A") providedExpr += ".w";
    }

    // 5. Auto-Casting final
    return CastGLSL(providedExpr, outputPin->Type, expectedType);
}

void MaterialEditorPanel::CompilePreviewShader() {
    // --- SÉCURITÉ : On ne compile pas de Shader OpenGL en Vulkan ---
    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) return;

    std::string fragCode = CompileMaterial();
    if (fragCode.empty()) return;

    // --- INJECTION DES SWITCHES ET DE LA MACRO PREVIEW ---
    std::string defines = "\n#define IS_PREVIEW_VIEWPORT\n"; // <-- NOUVEAU

    for (auto& node : m_Nodes) {
        if (node.Name == "StaticSwitchParameter" && node.BoolValue) {
            defines += "#define " + node.ParameterName + "\n";
        }
    }

    size_t pos = fragCode.find("#version");
    if (pos != std::string::npos) pos = fragCode.find('\n', pos) + 1;
    else pos = 0;

    fragCode.insert(pos, defines);

    std::filesystem::path cacheDir = Project::GetCacheDirectory();
    if (!std::filesystem::exists(cacheDir)) {
        std::filesystem::create_directories(cacheDir);
    }

    std::filesystem::path tempPath = cacheDir / "preview_material.frag";

    std::ofstream out(tempPath);
    out << fragCode;
    out.close();

    m_PreviewShader = std::make_shared<Shader>("shaders/default.vert", tempPath.string().c_str());
}

void MaterialEditorPanel::UpdateWildcardPins() {
    for (auto& node : m_Nodes) {
        // On demande au registre si ce type de noeud est un Wildcard
        auto it = MaterialNodeRegistry::GetRegistry().find(node.Name);
        if (it != MaterialNodeRegistry::GetRegistry().end() && it->second->IsWildcard()) {

            PinType highest = PinType::Float;
            for (auto& input : node.Inputs) {
                if (node.Name == "Mix" && input.Name == "Alpha") continue;

                for (auto& link : m_Links) {
                    if (link.EndPinID == input.ID) {
                        MaterialPin* outPin = FindPin(link.StartPinID);
                        if (outPin && outPin->Type > highest) highest = outPin->Type;
                    }
                }
            }
            for (auto& input : node.Inputs) {
                if (node.Name == "Mix" && input.Name == "Alpha") continue;
                input.Type = highest;
            }
            for (auto& output : node.Outputs) {
                output.Type = highest;
            }
        }
    }
}

void MaterialEditorPanel::OnImGuiMenuFile() {
    if (ImGui::MenuItem("Save Material", "Ctrl+S")) {
        Save();
    }
    if (ImGui::MenuItem("Save Material As...", "Ctrl+Shift+S")) {
        SaveAs();
    }
}

void MaterialEditorPanel::Save() {
    if (!m_CurrentPath.empty()) {
        Save(m_CurrentPath); // Appelle ta "vraie" fonction de sauvegarde
        std::cout << "[MaterialEditor] Material automatically saved!" << std::endl;
    }
}

void MaterialEditorPanel::SaveAs() {
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItem[1] = { { "Cool Engine Material", "cemat" } };

    // On ouvre la fenêtre (Tu pourrais remplacer le premier nullptr par le dossier "Content" de ton projet)
    if (NFD::SaveDialog(outPath, filterItem, 1, nullptr, nullptr) == NFD_OKAY)
    {
        std::filesystem::path filepath = outPath;

        // Sécurité : On force l'extension .cemat si l'utilisateur a oublié de l'écrire
        if (filepath.extension() != ".cemat") {
            filepath += ".cemat";
        }

        m_CurrentPath = filepath; // Le panel mémorise sa nouvelle maison

        // 1. On crie au moteur de changer le nom de l'onglet !
        if (OnPathChangedCallback) {
            OnPathChangedCallback(m_CurrentPath);
        }

        // 2. On sauvegarde la vraie data et on déclenche le Hot-Reload
        Save(m_CurrentPath);

        NFD::FreePath(outPath);
    }
}

void MaterialEditorPanel::OnUpdate(float deltaTime) {
    // C'est ici, en toute sécurité HORS d'ImGui, qu'on redimensionne et qu'on dessine la 3D !
    if (m_ViewportSize.x > 0 && m_ViewportSize.y > 0) {
        uint32_t width = (uint32_t)m_ViewportSize.x;
        uint32_t height = (uint32_t)m_ViewportSize.y;

        if (width != m_PreviewFramebuffer->GetSpecification().Width || height != m_PreviewFramebuffer->GetSpecification().Height) {
            m_PreviewFramebuffer->Resize(width, height);
        }
        RenderPreview3D();
    }
}