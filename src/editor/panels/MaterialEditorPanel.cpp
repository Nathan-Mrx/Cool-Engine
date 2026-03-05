#include "MaterialEditorPanel.h"

MaterialEditorPanel::MaterialEditorPanel() {
    // On crée le contexte de l'éditeur nodal
    ed::Config config;
    config.SettingsFile = "MaterialEditor.json"; // Sauvegarde la position des noeuds
    m_Context = ed::CreateEditor(&config);
}

MaterialEditorPanel::~MaterialEditorPanel() {
    ed::DestroyEditor(m_Context);
}

void MaterialEditorPanel::OnImGuiRender(bool& isOpen) {
    if (!isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Material Editor", &isOpen)) {

        // --- DÉBUT DE LA ZONE NODALE ---
        ed::SetCurrentEditor(m_Context);
        ed::Begin("My Material Graph");

        // On va créer un noeud "Master" bidon pour tester
        ed::BeginNode(1); // ID du noeud = 1
        ImGui::Text("Base Material");
        ImGui::Separator();

        ed::BeginPin(2, ed::PinKind::Input); // ID de la pin = 2
        ImGui::Text("-> Base Color");
        ed::EndPin();

        ed::BeginPin(3, ed::PinKind::Input); // ID de la pin = 3
        ImGui::Text("-> Roughness");
        ed::EndPin();
        ed::EndNode();

        // Un deuxième noeud pour s'amuser
        ed::BeginNode(4); // ID = 4
        ImGui::Text("Color");
        ImGui::Separator();

        ed::BeginPin(5, ed::PinKind::Output); // ID = 5
        ImGui::Text("RGB ->");
        ed::EndPin();
        ed::EndNode();

        ed::End();
        // --- FIN DE LA ZONE NODALE ---
    }
    ImGui::End();
}