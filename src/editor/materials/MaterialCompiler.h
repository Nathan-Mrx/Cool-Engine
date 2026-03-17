#pragma once

#include <string>
#include <vector>
#include "editor/materials/MaterialGraph.h"

class MaterialCompiler {
public:
    static std::string CompileToGLSL(const std::vector<MaterialNode>& nodes, const std::vector<MaterialLink>& links);

private:
    static std::string ReadTemplate(const std::string& filepath);
    static MaterialNode* FindNode(int id, const std::vector<MaterialNode>& nodes);
    static MaterialPin* FindPin(int id, const std::vector<MaterialNode>& nodes);

    // Retourne le code GLSL inline pour un pin d'entrée spécifique
    static std::string GenerateExpression(int pinId, const std::vector<MaterialNode>& nodes, const std::vector<MaterialLink>& links);
};
