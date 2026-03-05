#pragma once
#include "MaterialNodeRegistry.h"

// ==========================================
// 1. MASTER NODE
// ==========================================
struct BaseMaterialNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(BaseMaterialNodeDef)

    std::string GetName() const override { return "Base Material"; }
    std::string GetCategory() const override { return "Master"; }
    ImColor GetColor() const override { return ImColor(30, 80, 50, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Base Color", ed::PinKind::Input, PinType::Vec3 });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Normal", ed::PinKind::Input, PinType::Vec3 });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Metallic", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Roughness", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Specular", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "AO", ed::PinKind::Input, PinType::Float });
    }
};

// ==========================================
// 2. CONSTANTES & VARIABLES
// ==========================================
struct ColorNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(ColorNodeDef)

    std::string GetName() const override { return "Color"; }
    std::string GetCategory() const override { return "Constants"; }
    ImColor GetColor() const override { return ImColor(120, 100, 30, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "RGBA", ed::PinKind::Output, PinType::Vec4 });
    }
};

struct FloatNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(FloatNodeDef)

    std::string GetName() const override { return "Float"; }
    std::string GetCategory() const override { return "Constants"; }
    ImColor GetColor() const override { return ImColor(120, 100, 30, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Value", ed::PinKind::Output, PinType::Float });
    }
};

// ==========================================
// 3. TEXTURES
// ==========================================
struct Texture2DNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(Texture2DNodeDef)

    std::string GetName() const override { return "Texture2D"; }
    std::string GetCategory() const override { return "Texture"; }
    ImColor GetColor() const override { return ImColor(120, 40, 40, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "UV", ed::PinKind::Input, PinType::Vec2 });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "RGBA", ed::PinKind::Output, PinType::Vec4 });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "RGB", ed::PinKind::Output, PinType::Vec3 });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "R", ed::PinKind::Output, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "G", ed::PinKind::Output, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Output, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Output, PinType::Float });
    }
};

// ==========================================
// 4. MATHÉMATIQUES
// ==========================================
#define MATH_NODE(ClassName, NodeName) \
struct ClassName : public IMaterialNodeDef { \
    CEMAT_NODE(ClassName) \
    std::string GetName() const override { return NodeName; } \
    std::string GetCategory() const override { return "Math"; } \
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); } \
    void Initialize(MaterialNode& node, int& nextId) const override { \
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Float }); \
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Float }); \
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float }); \
    } \
};

MATH_NODE(MultiplyNodeDef, "Multiply")
MATH_NODE(AddNodeDef, "Add")
MATH_NODE(SubtractNodeDef, "Subtract")

struct MixNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(MixNodeDef)

    std::string GetName() const override { return "Mix"; }
    std::string GetCategory() const override { return "Math"; }
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Alpha", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
};

struct ClampNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(ClampNodeDef)

    std::string GetName() const override { return "Clamp"; }
    std::string GetCategory() const override { return "Math"; }
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Value", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Min", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Max", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
};

struct PowNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(PowNodeDef)

    std::string GetName() const override { return "Pow"; }
    std::string GetCategory() const override { return "Math"; }
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Base", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Exp", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
};

// ==========================================
// 5. UVS & COORDONNÉES
// ==========================================
struct TexCoordsNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(TexCoordsNodeDef)

    std::string GetName() const override { return "TexCoords"; }
    std::string GetCategory() const override { return "UVs"; }
    ImColor GetColor() const override { return ImColor(120, 50, 50, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "UV", ed::PinKind::Output, PinType::Vec2 });
    }
};

struct TilingAndOffsetNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(TilingAndOffsetNodeDef)

    std::string GetName() const override { return "TilingAndOffset"; }
    std::string GetCategory() const override { return "UVs"; }
    ImColor GetColor() const override { return ImColor(120, 50, 50, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "UV", ed::PinKind::Input, PinType::Vec2 });

        MaterialPin tiling = { ed::PinId(nextId++), node.ID, "Tiling", ed::PinKind::Input, PinType::Vec2 };
        tiling.Vec2Value = { 1.0f, 1.0f };
        node.Inputs.push_back(tiling);

        MaterialPin offset = { ed::PinId(nextId++), node.ID, "Offset", ed::PinKind::Input, PinType::Vec2 };
        offset.Vec2Value = { 0.0f, 0.0f };
        node.Inputs.push_back(offset);

        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "UV", ed::PinKind::Output, PinType::Vec2 });
    }
};

struct PannerNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(PannerNodeDef)

    std::string GetName() const override { return "Panner"; }
    std::string GetCategory() const override { return "UVs"; }
    ImColor GetColor() const override { return ImColor(120, 50, 50, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "UV", ed::PinKind::Input, PinType::Vec2 });

        MaterialPin speed = { ed::PinId(nextId++), node.ID, "Speed", ed::PinKind::Input, PinType::Vec2 };
        speed.Vec2Value = { 0.1f, 0.0f };
        node.Inputs.push_back(speed);

        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Time", ed::PinKind::Input, PinType::Float });

        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "UV", ed::PinKind::Output, PinType::Vec2 });
    }
};

// ==========================================
// 6. ANIMATION & TEMPS
// ==========================================
struct TimeNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(TimeNodeDef)

    std::string GetName() const override { return "Time"; }
    std::string GetCategory() const override { return "Animation"; }
    ImColor GetColor() const override { return ImColor(80, 40, 120, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Time", ed::PinKind::Output, PinType::Float });
    }
};

// ==========================================
// 7. UTILITAIRES
// ==========================================
struct RerouteNodeDef : public IMaterialNodeDef {
    CEMAT_NODE(RerouteNodeDef)

    std::string GetName() const override { return "Reroute"; }
    std::string GetCategory() const override { return "Utility"; }
    ImColor GetColor() const override { return ImColor(45, 55, 65, 255); } // Invisible de toute façon dans le rendu

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "", ed::PinKind::Output, PinType::Float });
    }
};