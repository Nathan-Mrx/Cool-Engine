#include "MaterialEditorPanel.h"

MaterialEditorPanel::MaterialEditorPanel() {
    ed::Config config;
    config.SettingsFile = "MaterialEditor.json"; // Sauvegarde la position de tes noeuds !
    m_Context = ed::CreateEditor(&config);
}

MaterialEditorPanel::~MaterialEditorPanel() {
    ed::DestroyEditor(m_Context);
}

void MaterialEditorPanel::OnImGuiRender(bool& isOpen) {
    if (!isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    // --- LE FIX : On enlève les marges pour que la grille touche les bords ---
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("Material Editor", &isOpen)) {

        ed::SetCurrentEditor(m_Context);

        // 0,0 indique qu'on remplit 100% de la fenêtre disponible
        ed::Begin("My Material Graph", ImVec2(0.0f, 0.0f));

        // Noeud 1
        ed::BeginNode(1);
            ImGui::Text("Base Material");
            ImGui::Separator();
            ed::BeginPin(2, ed::PinKind::Input);
                ImGui::Text("-> Base Color");
            ed::EndPin();
            ed::BeginPin(3, ed::PinKind::Input);
                ImGui::Text("-> Roughness");
            ed::EndPin();
        ed::EndNode();

        // Noeud 2
        ed::BeginNode(4);
            ImGui::Text("Color");
            ImGui::Separator();
            ed::BeginPin(5, ed::PinKind::Output);
                ImGui::Text("RGB ->");
            ed::EndPin();
        ed::EndNode();

        // --- LE FIX MAGIQUE : Placement et Focus de la caméra ---
        if (m_FirstFrame) {
            ed::SetNodePosition(1, ImVec2(250, 100)); // On le met à droite
            ed::SetNodePosition(4, ImVec2(50, 100));  // On le met à gauche
            ed::NavigateToContent(); // Ordre à la caméra de cadrer tous les noeuds
            m_FirstFrame = false;
        }

        ed::End();
        ed::SetCurrentEditor(nullptr); // Très bonne pratique de nettoyage
    }
    ImGui::End();
    ImGui::PopStyleVar();
}