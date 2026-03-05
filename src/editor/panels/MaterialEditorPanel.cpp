#include "MaterialEditorPanel.h"
#include <algorithm>

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
        // 3. LOGIQUE DE CRÉATION DE CÂBLE (Drag & Drop souris)
        // =========================================================
        if (ed::BeginCreate()) {
            ed::PinId startPinId = 0, endPinId = 0;
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                if (ed::AcceptNewItem()) {
                    m_Links.push_back({ ed::LinkId(GetNextId()), startPinId, endPinId });
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