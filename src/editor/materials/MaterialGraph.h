#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

enum class PinType {
    Float = 0,
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

    float FloatValue = 0.0f;
    glm::vec2 Vec2Value = { 1.0f, 1.0f }; 
    glm::vec3 Vec3Value = { 1.0f, 1.0f, 1.0f };
};

struct MaterialNode {
    ed::NodeId ID;
    std::string Name;
    std::vector<MaterialPin> Inputs;
    std::vector<MaterialPin> Outputs;

    // UI Spécifique
    glm::vec4 ColorValue = { 1.0f, 1.0f, 1.0f, 1.0f };
    float FloatValue = 0.0f;
    std::string TexturePath = "";
    uint32_t TextureID = 0;
};

struct MaterialLink {
    ed::LinkId ID;
    ed::PinId StartPinID;
    ed::PinId EndPinID;
};