#pragma once
#include <filesystem>
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <string>
#include <unordered_set>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace ed = ax::NodeEditor;

// --- LES STRUCTURES DE DONNÉES DU GRAPHE ---
enum class PinType {
    Float,
    Vec2,
    Vec3,
    Vec4
};

struct MaterialPin {
    ed::PinId ID;
    ed::NodeId NodeID;
    std::string Name;
    ed::PinKind Kind;
    PinType Type;

    // --- NOUVEAU : Valeurs par défaut si le pin n'est pas branché ---
    float FloatValue = 0.0f;
    glm::vec3 Vec3Value = {1.0f, 1.0f, 1.0f};
};

struct MaterialNode {
    ed::NodeId ID;
    std::string Name;
    std::vector<MaterialPin> Inputs;
    std::vector<MaterialPin> Outputs;

    float FloatValue = 1.0f;
    glm::vec4 ColorValue = {1.0f, 1.0f, 1.0f, 1.0f};

    std::string TexturePath = "";
    unsigned int TextureID = 0;
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

    MaterialNode* SpawnNode(const std::string& type, ImVec2 position);
    ImVec2 m_ContextPopupPos;

    MaterialPin* FindPin(ed::PinId id);

    // --- NOUVEAU : OUTILS DE COMPILATION ---
    MaterialNode* FindNode(ed::NodeId id);
    std::string CompileMaterial();
    std::string EvaluatePinGLSL(ed::PinId inputPinId, std::unordered_set<int>& visited, std::stringstream& bodyBuilder);

    ed::EditorContext* m_Context = nullptr;
    bool m_FirstFrame = true;

    // --- LA MÉMOIRE DYNAMIQUE ---
    std::vector<MaterialNode> m_Nodes;
    std::vector<MaterialLink> m_Links;

    // Générateur d'ID unique
    int m_NextId = 1;
    int GetNextId() { return m_NextId++; }

    ed::PinId m_NewNodeLinkPinId = 0;

    // --- NOUVEAU : Auto-Casting GLSL ---
    std::string GetGLSLType(PinType type) {
        if (type == PinType::Vec2) return "vec2";
        if (type == PinType::Vec3) return "vec3";
        if (type == PinType::Vec4) return "vec4";
        return "float";
    }

    std::string CastGLSL(const std::string& var, PinType from, PinType to) {
        if (from == to) return var;
        if (from == PinType::Float) {
            if (to == PinType::Vec2) return "vec2(" + var + ")";
            if (to == PinType::Vec3) return "vec3(" + var + ")";
            if (to == PinType::Vec4) return "vec4(" + var + ")";
        }
        if (from == PinType::Vec2) {
            if (to == PinType::Float) return "(" + var + ".x)";
            if (to == PinType::Vec3) return "vec3(" + var + ", 0.0)";
            if (to == PinType::Vec4) return "vec4(" + var + ", 0.0, 1.0)";
        }
        if (from == PinType::Vec3) {
            if (to == PinType::Float) return "(" + var + ".x)";
            if (to == PinType::Vec2) return "(" + var + ".xy)";
            if (to == PinType::Vec4) return "vec4(" + var + ", 1.0)";
        }
        if (from == PinType::Vec4) {
            if (to == PinType::Float) return "(" + var + ".x)";
            if (to == PinType::Vec2) return "(" + var + ".xy)";
            if (to == PinType::Vec3) return "(" + var + ".xyz)";
        }
        return var;
    }
};