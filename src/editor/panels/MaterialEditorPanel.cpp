#include "MaterialEditorPanel.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

MaterialEditorPanel::MaterialEditorPanel() {
    ed::Config config;
    // --- LE KILL SWITCH (Bloque la corruption de caméra) ---
    config.SettingsFile = nullptr;
    m_Context = ed::CreateEditor(&config);

    BuildDefaultNodes();
}

MaterialEditorPanel::~MaterialEditorPanel() {
    ed::DestroyEditor(m_Context);
}

void MaterialEditorPanel::BuildDefaultNodes() {
    MaterialNode baseNode;
    baseNode.ID = GetNextId();
    baseNode.Name = "Base Material";
    // Le setup PBR standard !
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "Base Color", ed::PinKind::Input, PinType::Vec3 });
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "Normal", ed::PinKind::Input, PinType::Vec3 });
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "Metallic", ed::PinKind::Input, PinType::Float });
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "Roughness", ed::PinKind::Input, PinType::Float });
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "Specular", ed::PinKind::Input, PinType::Float });
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "AO", ed::PinKind::Input, PinType::Float });
    m_Nodes.push_back(baseNode);
}

void MaterialEditorPanel::OnImGuiRender(bool& isOpen) {
    if (!isOpen) return;

    // On enlève les marges pour que la grille remplisse l'onglet
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("Material Editor", &isOpen)) {

        // --- NOUVEAU : BOUTON DE COMPILATION ---
        ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
        if (ImGui::Button("Compile to Console", ImVec2(150, 30))) {
            CompileMaterial();
        }
        ImGui::SameLine();

        // --- TEXTE DE DIAGNOSTIC ---
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[DEBUG] Graphe Dynamique Actif ! Noeuds: %zu | Liens: %zu", m_Nodes.size(), m_Links.size());



        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 100.0f) avail.x = 800.0f;
        if (avail.y < 100.0f) avail.y = 600.0f;

        ed::SetCurrentEditor(m_Context);
        ed::Begin("My Material Graph", avail);

        // =========================================================
        // 1. DESSINER LES NOEUDS DYNAMIQUEMENT
        // =========================================================
        for (auto& node : m_Nodes) {
            ed::BeginNode(node.ID);
            ImGui::Text("%s", node.Name.c_str());
            ImGui::Separator();

            for (auto& input : node.Inputs) {
                // 1. On dessine la pastille de connexion
                ed::BeginPin(input.ID, input.Kind);
                ImGui::Text("-> %s", input.Name.c_str());
                ed::EndPin();

                // 2. --- L'UI MAGIQUE FAÇON UNREAL ENGINE ---
                bool isConnected = false;
                for (auto& link : m_Links) {
                    if (link.EndPinID == input.ID) { isConnected = true; break; }
                }

                // S'il n'y a aucun câble, on affiche un champ texte direct !
                if (!isConnected) {
                    ImGui::SameLine();
                    ImGui::PushID((int)input.ID.Get()); // Sécurité anti-bug de clics

                    if (input.Type == PinType::Float) {
                        ImGui::PushItemWidth(60.0f);
                        ImGui::DragFloat("##v", &input.FloatValue, 0.01f);
                        ImGui::PopItemWidth();
                    } else if (input.Type == PinType::Vec3) {
                        ImGui::PushItemWidth(60.0f);
                        ImGui::ColorEdit3("##v", &input.Vec3Value[0], ImGuiColorEditFlags_NoInputs);
                        ImGui::PopItemWidth();
                    }
                    ImGui::PopID();
                }
            }

            for (auto& output : node.Outputs) {
                ed::BeginPin(output.ID, output.Kind);
                ImGui::Text("%s ->", output.Name.c_str());
                ed::EndPin();
            }
            ed::EndNode();
        }

        // =========================================================
        // 2. DESSINER LES CÂBLES EXISTANTS
        // =========================================================
        for (auto& link : m_Links) {
            ed::Link(link.ID, link.StartPinID, link.EndPinID);
        }

        // =========================================================
        // 3. LOGIQUE DE CRÉATION DE CÂBLE
        // =========================================================
        bool openNodeMenu = false; // Drapeau pour ouvrir le menu

        if (ed::BeginCreate(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 2.0f)) {
            ed::PinId startPinId = 0, endPinId = 0;

            // Si on relie deux connecteurs
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                // ... (Ton code de validation Règle 1, 2, 3 et 4 RESTE ICI, ne le supprime pas)
                auto startPin = FindPin(startPinId);
                auto endPin = FindPin(endPinId);

                if (startPin && endPin) {
                    if (startPin == endPin || startPin->NodeID == endPin->NodeID) { ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f); }
                    else if (startPin->Kind == endPin->Kind) { ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f); }
                    else if (startPin->Type != endPin->Type) { ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f); }
                    else {
                        if (ed::AcceptNewItem(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 2.0f)) {
                            if (startPin->Kind == ed::PinKind::Input) {
                                std::swap(startPin, endPin);
                                std::swap(startPinId, endPinId);
                            }
                            m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                                [endPinId](const MaterialLink& link) { return link.EndPinID == endPinId; }), m_Links.end());
                            m_Links.push_back({ ed::LinkId(GetNextId()), startPinId, endPinId });
                        }
                    }
                }
            }

            // --- NOUVEAU : Si on lâche un câble dans le vide ! ---
            ed::PinId newNodePinId = 0;
            if (ed::QueryNewNode(&newNodePinId)) {
                if (ed::AcceptNewItem()) {
                    m_NewNodeLinkPinId = newNodePinId;
                    m_ContextPopupPos = ed::ScreenToCanvas(ImGui::GetMousePos());
                    openNodeMenu = true; // On signale qu'il faut ouvrir le menu contextuel
                }
            }
        }
        ed::EndCreate();

        // =========================================================
        // 4. LOGIQUE DE SUPPRESSION (Câbles ET Noeuds !)
        // =========================================================
        if (ed::BeginDelete()) {
            // Suppression des câbles (Alt+Clic ou Suppr)
            ed::LinkId deletedLinkId = 0;
            while (ed::QueryDeletedLink(&deletedLinkId)) {
                if (ed::AcceptDeletedItem()) {
                    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                        [deletedLinkId](const MaterialLink& link) { return link.ID == deletedLinkId; }), m_Links.end());
                }
            }

            // --- NOUVEAU : Suppression des Noeuds (Touche Suppr) ---
            ed::NodeId deletedNodeId = 0;
            while (ed::QueryDeletedNode(&deletedNodeId)) {
                // On empêche la suppression du Base Material !
                auto node = FindNode(deletedNodeId);
                if (node && node->Name == "Base Material") {
                    ed::RejectDeletedItem();
                } else if (ed::AcceptDeletedItem()) {
                    // 1. On supprime tous les câbles branchés à ce noeud pour éviter les crashs
                    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                        [this, deletedNodeId](const MaterialLink& link) {
                            MaterialPin* p1 = FindPin(link.StartPinID);
                            MaterialPin* p2 = FindPin(link.EndPinID);
                            return (p1 && p1->NodeID == deletedNodeId) || (p2 && p2->NodeID == deletedNodeId);
                        }), m_Links.end());

                    // 2. On supprime le noeud de la mémoire
                    m_Nodes.erase(std::remove_if(m_Nodes.begin(), m_Nodes.end(),
                        [deletedNodeId](const MaterialNode& n) { return n.ID == deletedNodeId; }), m_Nodes.end());
                }
            }
        }
        ed::EndDelete();

        // =========================================================
        // 6. RACCOURCIS SOURIS SUR LES PINS (Alt + Clic)
        // =========================================================
        ed::PinId hoveredPin = ed::GetHoveredPin();
        if (hoveredPin) {
            // Si on clique gauche sur une Pin en survol...
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

                // Si la touche ALT est enfoncée, on coupe tout !
                if (ImGui::GetIO().KeyAlt) {
                    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                        [hoveredPin](const MaterialLink& link) {
                            return link.StartPinID == hoveredPin || link.EndPinID == hoveredPin;
                        }), m_Links.end());
                }
            }
        }

        // =========================================================
        // 5. MENU CONTEXTUEL & AUTO-CONNEXION
        // =========================================================
        ed::Suspend();

        // Clic droit classique dans le vide
        if (ed::ShowBackgroundContextMenu()) {
            m_NewNodeLinkPinId = 0; // Ce n'est pas un Drag&Drop
            m_ContextPopupPos = ed::ScreenToCanvas(ImGui::GetMousePos());
            ImGui::OpenPopup("CreateNewNode");
        }

        // Si on a lâché un câble dans le vide
        if (openNodeMenu) {
            ImGui::OpenPopup("CreateNewNode");
        }

        if (ImGui::BeginPopup("CreateNewNode")) {
            ImGui::TextDisabled("Create Node");
            ImGui::Separator();

            MaterialNode* spawnedNode = nullptr;

            if (ImGui::MenuItem("Color"))     { spawnedNode = SpawnNode("Color", m_ContextPopupPos); }
            if (ImGui::MenuItem("Texture2D")) { spawnedNode = SpawnNode("Texture2D", m_ContextPopupPos); }
            ImGui::Separator();
            if (ImGui::MenuItem("Multiply"))  { spawnedNode = SpawnNode("Multiply", m_ContextPopupPos); }
            if (ImGui::MenuItem("Add"))       { spawnedNode = SpawnNode("Add", m_ContextPopupPos); }
            if (ImGui::MenuItem("Subtract"))  { spawnedNode = SpawnNode("Subtract", m_ContextPopupPos); }
            if (ImGui::MenuItem("Mix"))       { spawnedNode = SpawnNode("Mix", m_ContextPopupPos); }
            if (ImGui::MenuItem("Clamp"))     { spawnedNode = SpawnNode("Clamp", m_ContextPopupPos); }
            if (ImGui::MenuItem("Pow"))       { spawnedNode = SpawnNode("Pow", m_ContextPopupPos); }
            ImGui::Separator();
            if (ImGui::MenuItem("Float"))     { spawnedNode = SpawnNode("Float", m_ContextPopupPos); }

            // --- NOUVEAU : AUTO-CONNEXION INTELLIGENTE ---
            if (spawnedNode && m_NewNodeLinkPinId.Get() != 0) {
                MaterialPin* startPin = FindPin(m_NewNodeLinkPinId);
                if (startPin) {
                    MaterialPin* targetPin = nullptr;
                    // On cherche une broche compatible sur le nouveau noeud (Même Type : Float, Vec3...)
                    if (startPin->Kind == ed::PinKind::Output) {
                        for (auto& pin : spawnedNode->Inputs) {
                            if (pin.Type == startPin->Type) { targetPin = &pin; break; }
                        }
                    } else {
                        for (auto& pin : spawnedNode->Outputs) {
                            if (pin.Type == startPin->Type) { targetPin = &pin; break; }
                        }
                    }

                    // Si on a trouvé une broche compatible, on les relie !
                    if (targetPin) {
                        auto inputPinId = (targetPin->Kind == ed::PinKind::Input) ? targetPin->ID : startPin->ID;
                        auto outputPinId = (targetPin->Kind == ed::PinKind::Output) ? targetPin->ID : startPin->ID;

                        // Sécurité : on débranche l'ancien câble si l'entrée était déjà occupée
                        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                            [inputPinId](const MaterialLink& link) { return link.EndPinID == inputPinId; }), m_Links.end());

                        m_Links.push_back({ ed::LinkId(GetNextId()), outputPinId, inputPinId });
                    }
                }
                m_NewNodeLinkPinId = 0; // Reset
            }
            ImGui::EndPopup();
        } else {
            // Si le joueur ferme le menu sans rien créer (en cliquant ailleurs)
            m_NewNodeLinkPinId = 0;
        }

        ed::Resume();

        // =========================================================
        // PLACEMENT INITIAL SANS NAVIGATETOCONTENT !
        // =========================================================
        if (m_FirstFrame && m_Nodes.size() >= 2) {
            ed::SetNodePosition(m_Nodes[0].ID, ImVec2(400, 100)); // Base Material à droite
            ed::SetNodePosition(m_Nodes[1].ID, ImVec2(50, 100));  // Color à gauche
            m_FirstFrame = false;
        }

        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    ImGui::End();
    ImGui::PopStyleVar();
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

MaterialNode* MaterialEditorPanel::SpawnNode(const std::string& type, ImVec2 position) {
    MaterialNode newNode;
    newNode.ID = GetNextId();
    newNode.Name = type;

    if (type == "Texture2D") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "UV", ed::PinKind::Input, PinType::Vec2 });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "RGBA", ed::PinKind::Output, PinType::Vec4 });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "RGB", ed::PinKind::Output, PinType::Vec3 });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "R", ed::PinKind::Output, PinType::Float });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "G", ed::PinKind::Output, PinType::Float });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "B", ed::PinKind::Output, PinType::Float });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "A", ed::PinKind::Output, PinType::Float });
    } else if (type == "Color") {
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "RGBA", ed::PinKind::Output, PinType::Vec4 });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "RGB", ed::PinKind::Output, PinType::Vec3 });
    } else if (type == "Float") {
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Value", ed::PinKind::Output, PinType::Float });
    } else if (type == "Multiply" || type == "Add") {
        // Pour l'instant, on force les maths simples en Vec3 (on fera des noeuds génériques plus tard)
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "A", ed::PinKind::Input, PinType::Vec3 });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "B", ed::PinKind::Input, PinType::Vec3 });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Result", ed::PinKind::Output, PinType::Vec3 });
    } else if (type == "Clamp") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Value", ed::PinKind::Input, PinType::Float });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Min", ed::PinKind::Input, PinType::Float });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Max", ed::PinKind::Input, PinType::Float });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Result", ed::PinKind::Output, PinType::Float });
    } else if (type == "Pow") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Base", ed::PinKind::Input, PinType::Float });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Exp", ed::PinKind::Input, PinType::Float });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Result", ed::PinKind::Output, PinType::Float });
    } else if (type == "Mix") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "A", ed::PinKind::Input, PinType::Vec3 });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "B", ed::PinKind::Input, PinType::Vec3 });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Alpha", ed::PinKind::Input, PinType::Float });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Result", ed::PinKind::Output, PinType::Vec3 });
    } else if (type == "Add") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "A", ed::PinKind::Input, PinType::Vec3 });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "B", ed::PinKind::Input, PinType::Vec3 });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Result", ed::PinKind::Output, PinType::Vec3 });
    } else if (type == "Subtract") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "A", ed::PinKind::Input, PinType::Vec3 });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "B", ed::PinKind::Input, PinType::Vec3 });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Result", ed::PinKind::Output, PinType::Vec3 });
    }

    m_Nodes.push_back(newNode);
    ed::SetNodePosition(newNode.ID, position);

    return &m_Nodes.back();
}

void MaterialEditorPanel::Save(const std::filesystem::path& path) {
    ed::SetCurrentEditor(m_Context);

    nlohmann::json data;
    data["Type"] = "MaterialGraph";
    data["NextID"] = m_NextId;

    data["GeneratedGLSL"] = CompileMaterial();

    auto& nodesOut = data["Nodes"];
    for (auto& node : m_Nodes) {
        nlohmann::json nodeJson;
        nodeJson["ID"] = (int)node.ID.Get();
        nodeJson["Name"] = node.Name;

        ImVec2 pos = ed::GetNodePosition(node.ID);
        nodeJson["Position"] = { pos.x, pos.y };

        // --- NOUVEAU : Sauvegarde des valeurs du noeud ---
        nodeJson["FloatValue"] = node.FloatValue;
        nodeJson["ColorValue"] = { node.ColorValue.r, node.ColorValue.g, node.ColorValue.b, node.ColorValue.a };
        nodeJson["TexturePath"] = node.TexturePath;

        for (auto& pin : node.Inputs) {
            nodeJson["Inputs"].push_back({
                {"ID", (int)pin.ID.Get()},
                {"Name", pin.Name},
                {"Type", (int)pin.Type},
                {"FloatValue", pin.FloatValue},
                {"Vec3Value", {pin.Vec3Value.r, pin.Vec3Value.g, pin.Vec3Value.b}}
            });
        }
        for (auto& pin : node.Outputs) {
            // --- NOUVEAU : Sauvegarde du PinType ---
            nodeJson["Outputs"].push_back({ {"ID", (int)pin.ID.Get()}, {"Name", pin.Name}, {"Type", (int)pin.Type} });
        }
        nodesOut.push_back(nodeJson);
    }

    auto& linksOut = data["Links"];
    for (auto& link : m_Links) {
        nlohmann::json linkJson;
        linkJson["ID"] = (int)link.ID.Get();
        linkJson["StartPinID"] = (int)link.StartPinID.Get();
        linkJson["EndPinID"] = (int)link.EndPinID.Get();
        linksOut.push_back(linkJson);
    }

    std::ofstream file(path);
    file << data.dump(4);

    ed::SetCurrentEditor(nullptr);
}

void MaterialEditorPanel::Load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    nlohmann::json data;
    try { file >> data; } catch(...) { return; }

    ed::SetCurrentEditor(m_Context);

    m_Nodes.clear();
    m_Links.clear();

    if (!data.contains("Nodes")) {
        m_NextId = 1;
        BuildDefaultNodes();
        ed::SetCurrentEditor(nullptr);
        return;
    }

    m_NextId = data.value("NextID", 1);

    for (auto& nodeJson : data["Nodes"]) {
        MaterialNode node;
        node.ID = ed::NodeId(nodeJson["ID"].get<int>());
        node.Name = nodeJson["Name"].get<std::string>();

        // --- NOUVEAU : Lecture des valeurs interactives ---
        if (nodeJson.contains("FloatValue")) node.FloatValue = nodeJson["FloatValue"].get<float>();
        if (nodeJson.contains("ColorValue")) {
            node.ColorValue = {
                nodeJson["ColorValue"][0],
                nodeJson["ColorValue"][1],
                nodeJson["ColorValue"][2],
                nodeJson["ColorValue"][3] // L'Alpha (A)
            };
        }
        if (nodeJson.contains("TexturePath")) node.TexturePath = nodeJson["TexturePath"].get<std::string>();

        if (nodeJson.contains("Inputs")) {
            for (auto& pinJson : nodeJson["Inputs"]) {
                PinType pType = pinJson.contains("Type") ? (PinType)pinJson["Type"].get<int>() : PinType::Vec3;

                MaterialPin newPin;
                newPin.ID = ed::PinId(pinJson["ID"].get<int>());
                newPin.NodeID = node.ID;
                newPin.Name = pinJson["Name"].get<std::string>();
                newPin.Kind = ed::PinKind::Input;
                newPin.Type = pType;

                // On récupère tes valeurs tapées à la main !
                if (pinJson.contains("FloatValue")) newPin.FloatValue = pinJson["FloatValue"].get<float>();
                if (pinJson.contains("Vec3Value")) newPin.Vec3Value = { pinJson["Vec3Value"][0], pinJson["Vec3Value"][1], pinJson["Vec3Value"][2] };

                node.Inputs.push_back(newPin);
            }
        }
        if (nodeJson.contains("Outputs")) {
            for (auto& pinJson : nodeJson["Outputs"]) {
                PinType pType = pinJson.contains("Type") ? (PinType)pinJson["Type"].get<int>() : PinType::Vec3;
                node.Outputs.push_back({ ed::PinId(pinJson["ID"].get<int>()), node.ID, pinJson["Name"].get<std::string>(), ed::PinKind::Output, pType });
            }
        }
        m_Nodes.push_back(node);

        if (nodeJson.contains("Position")) {
            float x = nodeJson["Position"][0].get<float>();
            float y = nodeJson["Position"][1].get<float>();
            ed::SetNodePosition(node.ID, ImVec2(x, y));
        }
    }

    if (data.contains("Links")) {
        for (auto& linkJson : data["Links"]) {
            MaterialLink link;
            link.ID = ed::LinkId(linkJson["ID"].get<int>());
            link.StartPinID = ed::PinId(linkJson["StartPinID"].get<int>());
            link.EndPinID = ed::PinId(linkJson["EndPinID"].get<int>());
            m_Links.push_back(link);
        }
    }
    ed::SetCurrentEditor(nullptr);
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

    // --- VARIABLES DE SORTIE (Match parfait avec default.frag) ---
    shaderCode << "layout(location = 0) out vec4 FragColor;\n";
    shaderCode << "layout(location = 1) out int EntityID;\n\n";

    // --- VARIABLES D'ENTRÉE (Depuis default.vert) ---
    shaderCode << "in vec3 vFragPos;\n";
    shaderCode << "in vec3 vNormal;\n"; // LE FIX EST ICI : Plus de tiret du bas !
    shaderCode << "in vec2 aTexCoords;\n\n"; // En préparation pour les textures

    // --- UNIFORMS (Lumière et Éditeur) ---
    shaderCode << "uniform vec3 uLightColor;\n";
    shaderCode << "uniform vec3 uLightDir;\n";
    shaderCode << "uniform float uAmbientStrength;\n";
    shaderCode << "uniform float uDiffuseStrength;\n";
    shaderCode << "uniform int uEntityID;\n";
    shaderCode << "uniform int uRenderMode;\n\n";

    // --- NOUVEAU : Déclaration des Sampler2D (Les textures) ---
    for (auto& node : m_Nodes) {
        if (node.Name == "Texture2D" && !node.TexturePath.empty()) {
            shaderCode << "uniform sampler2D u_Tex_" << node.ID.Get() << ";\n";
        }
    }

    shaderCode << "void main() {\n";

    std::unordered_set<int> visitedNodes;
    std::stringstream bodyBuilder;

    // 1. Calcul du graphe nodal
    std::string baseColorValue = "vec3(1.0, 0.0, 1.0)";
    if (!rootNode->Inputs.empty()) {
        baseColorValue = EvaluatePinGLSL(rootNode->Inputs[0].ID, visitedNodes, bodyBuilder);
    }

    // 2. Calcul de la Roughness (Pin 1)
    std::string roughnessValue = "0.5";
    if (rootNode->Inputs.size() > 1) {
        roughnessValue = EvaluatePinGLSL(rootNode->Inputs[1].ID, visitedNodes, bodyBuilder);
    }

    shaderCode << bodyBuilder.str();

    // 2. Gestion du mode Wireframe / Unlit
    shaderCode << "    if (uRenderMode == 1 || uRenderMode == 2) {\n";
    shaderCode << "        FragColor = vec4(" << baseColorValue << ", 1.0);\n";
    shaderCode << "        EntityID = uEntityID;\n";
    shaderCode << "        return;\n";
    shaderCode << "    }\n\n";

    // 3. Calcul de la lumière (Lambert)
    shaderCode << "    vec3 ambient = uAmbientStrength * uLightColor;\n";
    shaderCode << "    vec3 norm = normalize(vNormal);\n"; // Utilisation de la bonne variable !
    shaderCode << "    vec3 lightDir = normalize(-uLightDir);\n";
    shaderCode << "    float diff = max(dot(norm, lightDir), 0.0);\n";
    shaderCode << "    vec3 diffuse = diff * uDiffuseStrength * uLightColor;\n";

    // 4. Sortie Finale
    shaderCode << "    vec3 finalColor = (ambient + diffuse) * " << baseColorValue << ";\n";
    shaderCode << "    FragColor = vec4(finalColor, 1.0);\n";
    shaderCode << "    EntityID = uEntityID;\n"; // Le fix pour pouvoir cliquer sur le modèle !
    shaderCode << "}\n";

    return shaderCode.str();
}

// --- L'ALGORITHME MAGIQUE (Récursif) ---
std::string MaterialEditorPanel::EvaluatePinGLSL(ed::PinId inputPinId, std::unordered_set<int>& visited, std::stringstream& bodyBuilder) {
    MaterialLink* connectedLink = nullptr;
    for (auto& link : m_Links) {
        if (link.EndPinID == inputPinId) { connectedLink = &link; break; }
    }

    // Valeurs par défaut si rien n'est branché
    // --- LECTURE DES VALEURS MANUELLES ---
    MaterialPin* myInputPin = FindPin(inputPinId);
    if (!connectedLink) {
        if (myInputPin) {
            std::stringstream ss; // Sécurité pour formater les chiffres (éviter les virgules européennes)
            if (myInputPin->Type == PinType::Float) {
                ss << myInputPin->FloatValue;
                return ss.str();
            }
            if (myInputPin->Type == PinType::Vec3) {
                ss << "vec3(" << myInputPin->Vec3Value.r << ", "
                              << myInputPin->Vec3Value.g << ", "
                              << myInputPin->Vec3Value.b << ")";
                return ss.str();
            }
            if (myInputPin->Type == PinType::Vec2) return "vec2(0.0)";
        }
        return "vec3(0.0)";
    }

    MaterialPin* outputPin = FindPin(connectedLink->StartPinID);
    MaterialNode* sourceNode = FindNode(outputPin->NodeID);
    if (!sourceNode) return "vec3(0.0)";

    int sourceId = (int)sourceNode->ID.Get();

    // Génération du code du noeud source s'il n'a pas encore été visité
    if (visited.find(sourceId) == visited.end()) {
        visited.insert(sourceId);
        bodyBuilder << "    // Noeud: " << sourceNode->Name << " (ID: " << sourceId << ")\n";

        if (sourceNode->Name == "Color") {
            bodyBuilder << "    vec4 val_" << sourceId << " = vec4("
                        << sourceNode->ColorValue.r << ", " << sourceNode->ColorValue.g << ", "
                        << sourceNode->ColorValue.b << ", " << sourceNode->ColorValue.a << ");\n";
        }
        else if (sourceNode->Name == "Float") {
            bodyBuilder << "    float val_" << sourceId << " = " << sourceNode->FloatValue << ";\n";
        }
        else if (sourceNode->Name == "Multiply") {
            std::string a = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
            std::string b = EvaluatePinGLSL(sourceNode->Inputs[1].ID, visited, bodyBuilder);
            bodyBuilder << "    vec3 val_" << sourceId << " = " << a << " * " << b << ";\n";
        }
        else if (sourceNode->Name == "Add") {
            std::string a = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
            std::string b = EvaluatePinGLSL(sourceNode->Inputs[1].ID, visited, bodyBuilder);
            bodyBuilder << "    vec3 val_" << sourceId << " = " << a << " + " << b << ";\n";
        }
        else if (sourceNode->Name == "Subtract") {
            std::string a = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
            std::string b = EvaluatePinGLSL(sourceNode->Inputs[1].ID, visited, bodyBuilder);
            bodyBuilder << "    vec3 val_" << sourceId << " = " << a << " - " << b << ";\n";
        }
        else if (sourceNode->Name == "Mix") {
            std::string a = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
            std::string b = EvaluatePinGLSL(sourceNode->Inputs[1].ID, visited, bodyBuilder);
            std::string alpha = EvaluatePinGLSL(sourceNode->Inputs[2].ID, visited, bodyBuilder);
            bodyBuilder << "    vec3 val_" << sourceId << " = mix(" << a << ", " << b << ", " << alpha << ");\n";
        }
        else if (sourceNode->Name == "Clamp") {
            std::string val = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
            std::string minV = EvaluatePinGLSL(sourceNode->Inputs[1].ID, visited, bodyBuilder);
            std::string maxV = EvaluatePinGLSL(sourceNode->Inputs[2].ID, visited, bodyBuilder);
            bodyBuilder << "    float val_" << sourceId << " = clamp(" << val << ", " << minV << ", " << maxV << ");\n";
        }
        else if (sourceNode->Name == "Pow") {
            std::string base = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
            std::string exp = EvaluatePinGLSL(sourceNode->Inputs[1].ID, visited, bodyBuilder);
            bodyBuilder << "    float val_" << sourceId << " = pow(" << base << ", " << exp << ");\n";
        }
        else if (sourceNode->Name == "Texture2D") {
            std::string uv = EvaluatePinGLSL(sourceNode->Inputs[0].ID, visited, bodyBuilder);
            if (uv == "vec2(0.0)") uv = "vTexCoords"; // Si pas de noeud UV branché, on prend les UVs du modèle 3D !

            if (sourceNode->TexturePath.empty()) {
                bodyBuilder << "    vec4 tex_" << sourceId << " = vec4(1.0, 0.0, 1.0, 1.0);\n"; // Rose d'erreur si pas d'image
            } else {
                bodyBuilder << "    vec4 tex_" << sourceId << " = texture(u_Tex_" << sourceId << ", " << uv << ");\n";
            }
        }
        bodyBuilder << "\n";
    }

    // --- ROUTAGE DES SORTIES ---
    // Si c'est un noeud complexe (Color, Texture), on renvoie la bonne composante
    if (sourceNode->Name == "Color" || sourceNode->Name == "Texture2D") {
        std::string varName = (sourceNode->Name == "Color") ? "val_" : "tex_";
        varName += std::to_string(sourceId);

        if (outputPin->Name == "R") return varName + ".r";
        if (outputPin->Name == "G") return varName + ".g";
        if (outputPin->Name == "B") return varName + ".b";
        if (outputPin->Name == "A") return varName + ".a";
        if (outputPin->Name == "RGB") return varName + ".rgb";
        return varName; // RGBA
    }

    return "val_" + std::to_string(sourceId);
}