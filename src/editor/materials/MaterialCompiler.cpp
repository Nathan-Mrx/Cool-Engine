#include "MaterialCompiler.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "project/Project.h"

std::string MaterialCompiler::ReadTemplate(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

MaterialNode* MaterialCompiler::FindNode(int id, const std::vector<MaterialNode>& nodes) {
    for (auto& node : const_cast<std::vector<MaterialNode>&>(nodes)) {
        if ((int)node.ID.Get() == id) return &node;
    }
    return nullptr;
}

MaterialPin* MaterialCompiler::FindPin(int id, const std::vector<MaterialNode>& nodes) {
    for (auto& node : const_cast<std::vector<MaterialNode>&>(nodes)) {
        for (auto& pin : node.Inputs) if ((int)pin.ID.Get() == id) return &pin;
        for (auto& pin : node.Outputs) if ((int)pin.ID.Get() == id) return &pin;
    }
    return nullptr;
}

std::string MaterialCompiler::GenerateExpression(int destPinId, const std::vector<MaterialNode>& nodes, const std::vector<MaterialLink>& links) {
    for (const auto& link : links) {
        if ((int)link.EndPinID.Get() == destPinId) {
            MaterialPin* srcPin = FindPin((int)link.StartPinID.Get(), nodes);
            if (!srcPin) continue;

            MaterialNode* srcNode = FindNode((int)srcPin->NodeID.Get(), nodes);
            if (!srcNode) continue;

            // Constantes simples
            if (srcNode->Name == "Float") return std::to_string(srcNode->FloatValue);
            if (srcNode->Name == "Color") {
                return "vec4(" + std::to_string(srcNode->ColorValue.r) + ", " +
                                 std::to_string(srcNode->ColorValue.g) + ", " +
                                 std::to_string(srcNode->ColorValue.b) + ", " +
                                 std::to_string(srcNode->ColorValue.a) + ")";
            }
            if (srcNode->Name == "TextureCoord") return "inTexCoord";
            if (srcNode->Name == "Time") return "ubo.time"; // NOTA: Pas encore dans l'UBO, mais on anticipe

            // Noeuds mathématiques (récursif)
            if (srcNode->Name == "Multiply") {
                std::string A = GenerateExpression((int)srcNode->Inputs[0].ID.Get(), nodes, links);
                std::string B = GenerateExpression((int)srcNode->Inputs[1].ID.Get(), nodes, links);
                if (A.empty()) A = "1.0"; if (B.empty()) B = "1.0";
                return "(" + A + " * " + B + ")";
            }
            if (srcNode->Name == "Add") {
                std::string A = GenerateExpression((int)srcNode->Inputs[0].ID.Get(), nodes, links);
                std::string B = GenerateExpression((int)srcNode->Inputs[1].ID.Get(), nodes, links);
                if (A.empty()) A = "0.0"; if (B.empty()) B = "0.0";
                return "(" + A + " + " + B + ")";
            }
            if (srcNode->Name == "Sine") {
                std::string A = GenerateExpression((int)srcNode->Inputs[0].ID.Get(), nodes, links);
                if (A.empty()) A = "0.0";
                return "sin(" + A + ")";
            }
            
            // Textures (Les samplers sont rajoutés dans @INSERT_MATERIAL_UNIFORMS@)
            if (srcNode->Name == "Texture2D") {
                std::string uv = GenerateExpression((int)srcNode->Inputs[0].ID.Get(), nodes, links);
                if (uv.empty()) uv = "inTexCoord";
                std::string samplerName = "u_Tex_" + std::to_string((int)srcNode->ID.Get());
                return "texture(" + samplerName + ", " + uv + ")";
            }

            // Fallback pour les composants (ex: BreakOutFloatX) ou noeuds complexes
            return "vec4(1.0)"; 
        }
    }
    return ""; // Pas connecté
}

std::string MaterialCompiler::CompileToGLSL(const std::vector<MaterialNode>& nodes, const std::vector<MaterialLink>& links) {
    std::string templateStr = ReadTemplate((Project::GetProjectDirectory() / "shaders" / "material_template.frag").string());
    if (templateStr.empty()) {
        std::cerr << "[MaterialCompiler] Error: material_template.frag not found!" << std::endl;
        return "";
    }

    // 1. Trouver le "Base Material" (Master Node)
    MaterialNode* baseNode = nullptr;
    for (auto& n : const_cast<std::vector<MaterialNode>&>(nodes)) {
        if (n.Name == "Base Material") {
            baseNode = &n;
            break;
        }
    }

    if (!baseNode || baseNode->Inputs.size() < 6) return "";

    std::string uniformsCode = "";
    std::string logicCode = "";

    // 2. Extraire les Uniforms nécessaires (Textures définies)
    for (const auto& node : nodes) {
        if (node.Name == "Texture2D") {
            std::string samplerName = "u_Tex_" + std::to_string((int)node.ID.Get());
            // On les place dans le Set 2 (Le Set 0 est UBO, le Set 1 est pour PBR Environnement)
            uniformsCode += "layout(set = 2, binding = " + std::to_string(((int)node.ID.Get()) % 10) + ") uniform sampler2D " + samplerName + ";\n";
        }
    }

    // 3. Générer la logique du Base Material
    std::string c_albedo = GenerateExpression((int)baseNode->Inputs[0].ID.Get(), nodes, links);
    std::string c_normal = GenerateExpression((int)baseNode->Inputs[1].ID.Get(), nodes, links);
    std::string c_metallic = GenerateExpression((int)baseNode->Inputs[2].ID.Get(), nodes, links);
    std::string c_roughness = GenerateExpression((int)baseNode->Inputs[3].ID.Get(), nodes, links);
    std::string c_ao = GenerateExpression((int)baseNode->Inputs[4].ID.Get(), nodes, links);
    std::string c_emissive = GenerateExpression((int)baseNode->Inputs[5].ID.Get(), nodes, links);

    if (!c_albedo.empty()) logicCode += "    Albedo = (" + c_albedo + ").rgb * inColor;\n";
    if (!c_normal.empty()) {
        // Simple normal map decoding : xyz * 2.0 - 1.0 (TODO: TBN Matrix with per-vertex tangents)
        logicCode += "    Normal = normalize((" + c_normal + ").xyz * 2.0 - 1.0);\n";
    }
    if (!c_metallic.empty()) logicCode += "    Metallic = (" + c_metallic + ").r;\n";
    if (!c_roughness.empty()) logicCode += "    Roughness = (" + c_roughness + ").r;\n";
    if (!c_ao.empty()) logicCode += "    AO = (" + c_ao + ").r;\n";
    if (!c_emissive.empty()) logicCode += "    Emissive = (" + c_emissive + ").rgb;\n";

    // 4. Remplacer les Tags dans le Template
    size_t uniformPos = templateStr.find("// @INSERT_MATERIAL_UNIFORMS@");
    if (uniformPos != std::string::npos) {
        templateStr.replace(uniformPos, 29, uniformsCode);
    }

    size_t logicPos = templateStr.find("// @INSERT_MATERIAL_LOGIC@");
    if (logicPos != std::string::npos) {
        templateStr.replace(logicPos, 26, logicCode);
    }

    return templateStr;
}
