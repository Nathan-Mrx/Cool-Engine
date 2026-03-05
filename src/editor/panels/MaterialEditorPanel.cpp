#include "MaterialEditorPanel.h"

MaterialEditorPanel::MaterialEditorPanel() {
    ed::Config config;
    // --- LE KILL SWITCH : On coupe le lien avec le fichier corrompu ! ---
    config.SettingsFile = nullptr;
    m_Context = ed::CreateEditor(&config);
}

MaterialEditorPanel::~MaterialEditorPanel() {
    ed::DestroyEditor(m_Context);
}

void MaterialEditorPanel::OnImGuiRender(bool& isOpen) {
    if (!isOpen) return;

    if (ImGui::Begin("Material Editor", &isOpen)) {

        // --- TEXTE DE DIAGNOSTIC ---
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[DEBUG] Material Editor Actif ! Taille : %.1f x %.1f", avail.x, avail.y);

        // Sécurité anti-crash
        if (avail.x < 100.0f) avail.x = 800.0f;
        if (avail.y < 100.0f) avail.y = 600.0f;

        ed::SetCurrentEditor(m_Context);

        // On utilise la taille sécurisée
        ed::Begin("My Material Graph", avail);

        // Noeud 1
        ed::BeginNode(ed::NodeId(1));
            ImGui::Text("Base Material");
            ImGui::Separator();
            ed::BeginPin(ed::PinId(2), ed::PinKind::Input);
                ImGui::Text("-> Base Color");
            ed::EndPin();
            ed::BeginPin(ed::PinId(3), ed::PinKind::Input);
                ImGui::Text("-> Roughness");
            ed::EndPin();
        ed::EndNode();

        // Noeud 2
        ed::BeginNode(ed::NodeId(4));
            ImGui::Text("Color");
            ImGui::Separator();
            ed::BeginPin(ed::PinId(5), ed::PinKind::Output);
                ImGui::Text("RGB ->");
            ed::EndPin();
        ed::EndNode();

        // --- PLACEMENT FORCÉ (On zappe la caméra capricieuse) ---
        ed::SetNodePosition(ed::NodeId(1), ImVec2(400, 100));
        ed::SetNodePosition(ed::NodeId(4), ImVec2(50, 100));

        if (m_FirstFrame) {
            ed::NavigateToContent(0.0f); // Recadrage instantané
            m_FirstFrame = false;
        }

        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    ImGui::End();
}