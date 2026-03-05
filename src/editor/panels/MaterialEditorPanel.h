#pragma once
#include <filesystem>
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

// --- LES STRUCTURES DE DONNÉES DU GRAPHE ---
struct MaterialPin {
    ed::PinId ID;
    ed::NodeId NodeID;
    std::string Name;
    ed::PinKind Kind; // Input ou Output
};

struct MaterialNode {
    ed::NodeId ID;
    std::string Name;
    std::vector<MaterialPin> Inputs;
    std::vector<MaterialPin> Outputs;
};

struct MaterialLink {
    ed::LinkId ID;
    ed::PinId StartPinID;
    ed::PinId EndPinID;
};

class MaterialEditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel();

    void OnImGuiRender(bool& isOpen);

    void Save(const std::filesystem::path& path);
    void Load(const std::filesystem::path& path);

private:
    void BuildDefaultNodes();

    void SpawnNode(const std::string& type, ImVec2 position);
    ImVec2 m_ContextPopupPos;

    MaterialPin* FindPin(ed::PinId id);

    ed::EditorContext* m_Context = nullptr;
    bool m_FirstFrame = true;

    // --- LA MÉMOIRE DYNAMIQUE ---
    std::vector<MaterialNode> m_Nodes;
    std::vector<MaterialLink> m_Links;

    // Générateur d'ID unique
    int m_NextId = 1;
    int GetNextId() { return m_NextId++; }
};