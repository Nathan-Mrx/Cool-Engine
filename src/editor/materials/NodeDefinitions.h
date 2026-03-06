#pragma once
#include "MaterialNodeRegistry.h"

// ==========================================
// 1. MASTER NODE
// ==========================================
CEMAT_NODE()
struct BaseMaterialNodeDef : public IMaterialNodeDef {
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
    // Pas de GLSL généré directement, le CompileMaterial s'en occupe !
};

// ==========================================
// 2. CONSTANTES & VARIABLES
// ==========================================
CEMAT_NODE()
struct ColorNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Color"; }
    std::string GetCategory() const override { return "Constants"; }
    ImColor GetColor() const override { return ImColor(120, 100, 30, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "RGBA", ed::PinKind::Output, PinType::Vec4 });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        bodyBuilder << "    vec4 " << varName << " = vec4(" << node.ColorValue.r << ", " << node.ColorValue.g << ", " << node.ColorValue.b << ", " << node.ColorValue.a << ");\n";
        return varName;
    }
};

CEMAT_NODE()
struct FloatNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Float"; }
    std::string GetCategory() const override { return "Constants"; }
    ImColor GetColor() const override { return ImColor(120, 100, 30, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Value", ed::PinKind::Output, PinType::Float });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        bodyBuilder << "    float " << varName << " = " << node.FloatValue << ";\n";
        return varName;
    }
};

// ==========================================
// 3. TEXTURES
// ==========================================
CEMAT_NODE()
struct Texture2DNodeDef : public IMaterialNodeDef {
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
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string uv = evaluateInput(0, "vTexCoords"); // Magie : vTexCoords automatique si non branché !
        if (node.TexturePath.empty()) bodyBuilder << "    vec4 " << varName << " = vec4(1.0, 0.0, 1.0, 1.0);\n";
        else bodyBuilder << "    vec4 " << varName << " = texture(u_Tex_" << node.ID.Get() << ", " << uv << ");\n";
        return varName;
    }
};

// ==========================================
// 4. MATHÉMATIQUES
// ==========================================
#define MATH_NODE(ClassName, NodeName, Op) \
CEMAT_NODE() \
struct ClassName : public IMaterialNodeDef { \
    std::string GetName() const override { return NodeName; } \
    std::string GetCategory() const override { return "Math"; } \
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); } \
    void Initialize(MaterialNode& node, int& nextId) const override { \
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Float }); \
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Float }); \
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float }); \
    } \
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override { \
        std::string varName = "val_" + std::to_string((int)node.ID.Get()); \
        std::string a = evaluateInput(0, ""); \
        std::string b = evaluateInput(1, ""); \
        std::string t = GetGLSLTypeStr(node.Outputs[0].Type); \
        bodyBuilder << "    " << t << " " << varName << " = " << a << " " << Op << " " << b << ";\n"; \
        return varName; \
    } \
};

MATH_NODE(MultiplyNodeDef, "Multiply", "*")
MATH_NODE(AddNodeDef, "Add", "+")
MATH_NODE(SubtractNodeDef, "Subtract", "-")

CEMAT_NODE()
struct MixNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Mix"; }
    std::string GetCategory() const override { return "Math"; }
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Alpha", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string a = evaluateInput(0, "");
        std::string b = evaluateInput(1, "");
        std::string alpha = evaluateInput(2, "");
        std::string t = GetGLSLTypeStr(node.Outputs[0].Type);
        bodyBuilder << "    " << t << " " << varName << " = mix(" << a << ", " << b << ", " << alpha << ");\n";
        return varName;
    }
};

CEMAT_NODE()
struct ClampNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Clamp"; }
    std::string GetCategory() const override { return "Math"; }
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Value", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Min", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Max", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string val = evaluateInput(0, "");
        std::string minV = evaluateInput(1, "");
        std::string maxV = evaluateInput(2, "");
        std::string t = GetGLSLTypeStr(node.Outputs[0].Type);
        bodyBuilder << "    " << t << " " << varName << " = clamp(" << val << ", " << minV << ", " << maxV << ");\n";
        return varName;
    }
};

CEMAT_NODE()
struct PowNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Pow"; }
    std::string GetCategory() const override { return "Math"; }
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Base", ed::PinKind::Input, PinType::Float });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "Exp", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string base = evaluateInput(0, "");
        std::string exp = evaluateInput(1, "");
        std::string t = GetGLSLTypeStr(node.Outputs[0].Type);
        bodyBuilder << "    " << t << " " << varName << " = pow(" << base << ", " << exp << ");\n";
        return varName;
    }
};

// ==========================================
// 5. UVS & COORDONNÉES
// ==========================================
CEMAT_NODE()
struct TexCoordsNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "TexCoords"; }
    std::string GetCategory() const override { return "UVs"; }
    ImColor GetColor() const override { return ImColor(120, 50, 50, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "UV", ed::PinKind::Output, PinType::Vec2 });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        bodyBuilder << "    vec2 " << varName << " = vTexCoords;\n";
        return varName;
    }
};

CEMAT_NODE()
struct TilingAndOffsetNodeDef : public IMaterialNodeDef {
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
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string uv = evaluateInput(0, "vTexCoords");
        std::string tiling = evaluateInput(1, "");
        std::string offset = evaluateInput(2, "");
        bodyBuilder << "    vec2 " << varName << " = (" << uv << " * " << tiling << ") + " << offset << ";\n";
        return varName;
    }
};

CEMAT_NODE()
struct PannerNodeDef : public IMaterialNodeDef {
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
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string uv = evaluateInput(0, "vTexCoords");
        std::string speed = evaluateInput(1, "");
        std::string time = evaluateInput(2, "uTime");
        bodyBuilder << "    vec2 " << varName << " = " << uv << " + (" << speed << " * " << time << ");\n";
        return varName;
    }
};

// ==========================================
// 6. ANIMATION & TEMPS
// ==========================================
CEMAT_NODE()
struct TimeNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Time"; }
    std::string GetCategory() const override { return "Animation"; }
    ImColor GetColor() const override { return ImColor(80, 40, 120, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Time", ed::PinKind::Output, PinType::Float });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        bodyBuilder << "    float " << varName << " = uTime;\n";
        return varName;
    }
};

// ==========================================
// 7. UTILITAIRES
// ==========================================
CEMAT_NODE()
struct RerouteNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Reroute"; }
    std::string GetCategory() const override { return "Utility"; }
    ImColor GetColor() const override { return ImColor(45, 55, 65, 255); }

    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "", ed::PinKind::Input, PinType::Float });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "", ed::PinKind::Output, PinType::Float });
    }
    // Le Reroute est bypassé par le compilateur, mais au cas où :
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        return evaluateInput(0, "0.0");
    }
};

// --- MACROS POUR LES NOEUDS A 1 ENTREE (Sin, Cos, Abs...) ---
#define MATH_NODE_1_IN_EXPR(ClassName, NodeName, Expr) \
CEMAT_NODE() \
struct ClassName : public IMaterialNodeDef { \
    std::string GetName() const override { return NodeName; } \
    std::string GetCategory() const override { return "Math"; } \
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); } \
    bool IsWildcard() const override { return true; } \
    void Initialize(MaterialNode& node, int& nextId) const override { \
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "In", ed::PinKind::Input, PinType::Float }); \
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Out", ed::PinKind::Output, PinType::Float }); \
    } \
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override { \
        std::string varName = "val_" + std::to_string((int)node.ID.Get()); \
        std::string inVal = evaluateInput(0, "0.0"); \
        std::string t = GetGLSLTypeStr(node.Outputs[0].Type); \
        bodyBuilder << "    " << t << " " << varName << " = " << Expr << ";\n"; \
        return varName; \
    } \
};

// --- MACROS POUR LES NOEUDS A 2 ENTREES (Min, Max, Mod...) ---
#define MATH_NODE_2_IN_EXPR(ClassName, NodeName, Expr) \
CEMAT_NODE() \
struct ClassName : public IMaterialNodeDef { \
    std::string GetName() const override { return NodeName; } \
    std::string GetCategory() const override { return "Math"; } \
    ImColor GetColor() const override { return ImColor(30, 70, 100, 255); } \
    bool IsWildcard() const override { return true; } \
    void Initialize(MaterialNode& node, int& nextId) const override { \
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Float }); \
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Float }); \
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float }); \
    } \
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override { \
        std::string varName = "val_" + std::to_string((int)node.ID.Get()); \
        std::string a = evaluateInput(0, "0.0"); \
        std::string b = evaluateInput(1, "0.0"); \
        std::string t = GetGLSLTypeStr(node.Outputs[0].Type); \
        bodyBuilder << "    " << t << " " << varName << " = " << Expr << ";\n"; \
        return varName; \
    } \
};

// === LES 9 NOUVEAUX NOEUDS MATHÉMATIQUES ===
MATH_NODE_1_IN_EXPR(OneMinusNodeDef, "OneMinus", "1.0 - " + inVal)
MATH_NODE_1_IN_EXPR(SinNodeDef, "Sin", "sin(" + inVal + ")")
MATH_NODE_1_IN_EXPR(CosNodeDef, "Cos", "cos(" + inVal + ")")
MATH_NODE_1_IN_EXPR(AbsNodeDef, "Abs", "abs(" + inVal + ")")
MATH_NODE_1_IN_EXPR(FractNodeDef, "Fract", "fract(" + inVal + ")")

MATH_NODE_2_IN_EXPR(MinNodeDef, "Min", "min(" + a + ", " + b + ")")
MATH_NODE_2_IN_EXPR(MaxNodeDef, "Max", "max(" + a + ", " + b + ")")
MATH_NODE_2_IN_EXPR(ModNodeDef, "Modulo", "mod(" + a + ", " + b + ")")
MATH_NODE_2_IN_EXPR(StepNodeDef, "Step", "step(" + a + ", " + b + ")")


// ==========================================
// 5. VECTOR MATH (Pas de Wildcard, types stricts !)
// ==========================================
CEMAT_NODE()
struct DotProductNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Dot"; }
    std::string GetCategory() const override { return "Vector Math"; }
    ImColor GetColor() const override { return ImColor(40, 80, 120, 255); }
    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Vec3 });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Vec3 });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string a = evaluateInput(0, "vec3(0.0)");
        std::string b = evaluateInput(1, "vec3(0.0)");
        bodyBuilder << "    float " << varName << " = dot(" << a << ", " << b << ");\n";
        return varName;
    }
};

CEMAT_NODE()
struct CrossProductNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Cross"; }
    std::string GetCategory() const override { return "Vector Math"; }
    ImColor GetColor() const override { return ImColor(40, 80, 120, 255); }
    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Vec3 });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Vec3 });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Vec3 });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string a = evaluateInput(0, "vec3(0.0)");
        std::string b = evaluateInput(1, "vec3(0.0)");
        bodyBuilder << "    vec3 " << varName << " = cross(" << a << ", " << b << ");\n";
        return varName;
    }
};

CEMAT_NODE()
struct DistanceNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Distance"; }
    std::string GetCategory() const override { return "Vector Math"; }
    ImColor GetColor() const override { return ImColor(40, 80, 120, 255); }
    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "A", ed::PinKind::Input, PinType::Vec3 });
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "B", ed::PinKind::Input, PinType::Vec3 });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Float });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string a = evaluateInput(0, "vec3(0.0)");
        std::string b = evaluateInput(1, "vec3(0.0)");
        bodyBuilder << "    float " << varName << " = distance(" << a << ", " << b << ");\n";
        return varName;
    }
};

CEMAT_NODE()
struct NormalizeNodeDef : public IMaterialNodeDef {
    std::string GetName() const override { return "Normalize"; }
    std::string GetCategory() const override { return "Vector Math"; }
    ImColor GetColor() const override { return ImColor(40, 80, 120, 255); }
    void Initialize(MaterialNode& node, int& nextId) const override {
        node.Inputs.push_back({ ed::PinId(nextId++), node.ID, "V", ed::PinKind::Input, PinType::Vec3 });
        node.Outputs.push_back({ ed::PinId(nextId++), node.ID, "Result", ed::PinKind::Output, PinType::Vec3 });
    }
    std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const override {
        std::string varName = "val_" + std::to_string((int)node.ID.Get());
        std::string v = evaluateInput(0, "vec3(0.0, 0.0, 1.0)");
        bodyBuilder << "    vec3 " << varName << " = normalize(" << v << ");\n";
        return varName;
    }
};