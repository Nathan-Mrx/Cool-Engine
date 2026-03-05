#include "MaterialEditorPanel.h"
#include <algorithm>
#include <fstream>
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
    // 1. Noeud Principal
    MaterialNode baseNode;
    baseNode.ID = GetNextId();
    baseNode.Name = "Base Material";
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "Base Color", ed::PinKind::Input });
    baseNode.Inputs.push_back({ ed::PinId(GetNextId()), baseNode.ID, "Roughness", ed::PinKind::Input });
    m_Nodes.push_back(baseNode);

    // 2. Noeud de Couleur
    MaterialNode colorNode;
    colorNode.ID = GetNextId();
    colorNode.Name = "Color";
    colorNode.Outputs.push_back({ ed::PinId(GetNextId()), colorNode.ID, "RGB", ed::PinKind::Output });
    m_Nodes.push_back(colorNode);
}

void MaterialEditorPanel::OnImGuiRender(bool& isOpen) {
    if (!isOpen) return;

    // On enlève les marges pour que la grille remplisse l'onglet
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("Material Editor", &isOpen)) {

        // --- TEXTE DE DIAGNOSTIC ---
        ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
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
                ed::BeginPin(input.ID, input.Kind);
                ImGui::Text("-> %s", input.Name.c_str());
                ed::EndPin();
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
        // 3. LOGIQUE DE CRÉATION DE CÂBLE (Drag & Drop avec Validation)
        // =========================================================
        if (ed::BeginCreate(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 2.0f)) {
            ed::PinId startPinId = 0, endPinId = 0;

            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                auto startPin = FindPin(startPinId);
                auto endPin = FindPin(endPinId);

                if (startPin && endPin) {
                    // Règle 1 : Pas de connexion sur soi-même ou sur le même noeud
                    if (startPin == endPin || startPin->NodeID == endPin->NodeID) {
                        ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f); // Croix rouge !
                    }
                    // Règle 2 : Pas de Input->Input ou Output->Output
                    else if (startPin->Kind == endPin->Kind) {
                        ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f); // Croix rouge !
                    }
                    // Si toutes les règles sont respectées, on propose un lien vert
                    else {
                        // Le survol est valide, si on relâche la souris on accepte !
                        if (ed::AcceptNewItem(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 2.0f)) {

                            // On s'assure que startPin est toujours l'Output, et endPin l'Input
                            // (l'utilisateur peut tirer le câble à l'envers !)
                            if (startPin->Kind == ed::PinKind::Input) {
                                std::swap(startPin, endPin);
                                std::swap(startPinId, endPinId);
                            }

                            // Règle 3 : Un Input ne peut avoir qu'un seul câble !
                            // On détruit tout câble existant qui serait déjà branché sur cet Input.
                            m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                                [endPinId](const MaterialLink& link) { return link.EndPinID == endPinId; }),
                                m_Links.end());

                            // On enregistre le nouveau câble parfait
                            m_Links.push_back({ ed::LinkId(GetNextId()), startPinId, endPinId });
                        }
                    }
                }
            }
        }
        ed::EndCreate();

        // =========================================================
        // 4. LOGIQUE DE SUPPRESSION DE CÂBLE (Alt + Clic)
        // =========================================================
        if (ed::BeginDelete()) {
            ed::LinkId deletedLinkId = 0;
            while (ed::QueryDeletedLink(&deletedLinkId)) {
                if (ed::AcceptDeletedItem()) {
                    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                        [deletedLinkId](const MaterialLink& link) { return link.ID == deletedLinkId; }),
                        m_Links.end());
                }
            }
        }
        ed::EndDelete();

        // =========================================================
        // 5. MENU CONTEXTUEL (CLIC DROIT)
        // =========================================================
        // On "suspend" l'éditeur nodal pour dessiner une fenêtre ImGui standard par-dessus
        ed::Suspend();

        if (ed::ShowBackgroundContextMenu()) {
            ImGui::OpenPopup("CreateNewNode");
            // On traduit les pixels de l'écran en coordonnées du monde nodal !
            m_ContextPopupPos = ed::ScreenToCanvas(ImGui::GetMousePos());
        }

        if (ImGui::BeginPopup("CreateNewNode")) {
            ImGui::TextDisabled("Create Node");
            ImGui::Separator();

            if (ImGui::MenuItem("Texture2D")) { SpawnNode("Texture2D", m_ContextPopupPos); }
            if (ImGui::MenuItem("Multiply"))  { SpawnNode("Multiply", m_ContextPopupPos); }
            if (ImGui::MenuItem("Float"))     { SpawnNode("Float", m_ContextPopupPos); }

            ImGui::EndPopup();
        }

        // On rend la main à l'éditeur nodal
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

void MaterialEditorPanel::SpawnNode(const std::string& type, ImVec2 position) {
    MaterialNode newNode;
    newNode.ID = GetNextId();
    newNode.Name = type;

    if (type == "Texture2D") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "UV", ed::PinKind::Input });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "RGB", ed::PinKind::Output });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "R", ed::PinKind::Output });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "G", ed::PinKind::Output });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "B", ed::PinKind::Output });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "A", ed::PinKind::Output });
    } else if (type == "Multiply") {
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "A", ed::PinKind::Input });
        newNode.Inputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "B", ed::PinKind::Input });
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Result", ed::PinKind::Output });
    } else if (type == "Float") {
        newNode.Outputs.push_back({ ed::PinId(GetNextId()), newNode.ID, "Value", ed::PinKind::Output });
    }

    m_Nodes.push_back(newNode);

    // On place le noeud exactement là où était la souris !
    ed::SetNodePosition(newNode.ID, position);
}

void MaterialEditorPanel::Save(const std::filesystem::path& path) {
    ed::SetCurrentEditor(m_Context); // On active le contexte pour pouvoir lire les positions

    nlohmann::json data;
    data["Type"] = "MaterialGraph";
    data["NextID"] = m_NextId;

    auto& nodesOut = data["Nodes"];
    for (auto& node : m_Nodes) {
        nlohmann::json nodeJson;
        nodeJson["ID"] = (int)node.ID.Get();
        nodeJson["Name"] = node.Name;

        // On sauvegarde la position de la boîte !
        ImVec2 pos = ed::GetNodePosition(node.ID);
        nodeJson["Position"] = { pos.x, pos.y };

        for (auto& pin : node.Inputs) {
            nodeJson["Inputs"].push_back({ {"ID", (int)pin.ID.Get()}, {"Name", pin.Name} });
        }
        for (auto& pin : node.Outputs) {
            nodeJson["Outputs"].push_back({ {"ID", (int)pin.ID.Get()}, {"Name", pin.Name} });
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
    file << data.dump(4); // Indentation de 4 espaces pour faire joli

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

    // Si c'est un matériau tout neuf (créé via le clic droit du Content Browser)
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

        if (nodeJson.contains("Inputs")) {
            for (auto& pinJson : nodeJson["Inputs"]) {
                node.Inputs.push_back({ ed::PinId(pinJson["ID"].get<int>()), node.ID, pinJson["Name"].get<std::string>(), ed::PinKind::Input });
            }
        }
        if (nodeJson.contains("Outputs")) {
            for (auto& pinJson : nodeJson["Outputs"]) {
                node.Outputs.push_back({ ed::PinId(pinJson["ID"].get<int>()), node.ID, pinJson["Name"].get<std::string>(), ed::PinKind::Output });
            }
        }
        m_Nodes.push_back(node);

        // On replace le noeud exactement là où il était
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