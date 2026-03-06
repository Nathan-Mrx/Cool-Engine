#pragma once
#include "MaterialGraph.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <map>
#include <functional> // <-- NOUVEAU
#include <sstream>    // <-- NOUVEAU

// --- LE TAG C.H.T (Cool Header Tool) ---
// À placer juste au-dessus d'une struct/class pour l'enregistrer dans l'éditeur.
#define CEMAT_NODE()

// --- LE BOUCLIER ANTI-PYTHON ---
// Ceci n'est ni struct ni class, la regex Python va échouer et ignorer l'interface !
using CHT_Blocker = int;

// --- LE MOULE (Interface) ---
class IMaterialNodeDef {
public:
    virtual ~IMaterialNodeDef() = default;

    virtual std::string GetName() const = 0;
    virtual std::string GetCategory() const { return "General"; }
    virtual ImColor GetColor() const { return ImColor(45, 55, 65, 255); }

    // --- NOUVEAU : Dit au moteur si ce noeud adapte son type dynamiquement ---
    virtual bool IsWildcard() const { return false; }

    virtual void Initialize(MaterialNode& node, int& nextId) const = 0;

    // --- LA NOUVELLE MAGIE DE COMPILATION ---
    virtual std::string GenerateGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) const {
        return "";
    }
};

class MaterialNodeRegistry {
public:
    static void RegisterAllNodes();

    static std::unordered_map<std::string, std::shared_ptr<IMaterialNodeDef>>& GetRegistry() {
        static std::unordered_map<std::string, std::shared_ptr<IMaterialNodeDef>> registry;
        return registry;
    }

    static void Register(std::shared_ptr<IMaterialNodeDef> def) { GetRegistry()[def->GetName()] = def; }

    static bool CreateNode(const std::string& name, int& nextId, MaterialNode& outNode) {
        if (GetRegistry().find(name) != GetRegistry().end()) {
            outNode.ID = ed::NodeId(nextId++);
            outNode.Name = name;
            outNode.Inputs.reserve(16);
            outNode.Outputs.reserve(16);
            GetRegistry()[name]->Initialize(outNode, nextId);
            return true;
        }
        return false;
    }

    static ImColor GetNodeColor(const std::string& name) {
        if (GetRegistry().find(name) != GetRegistry().end()) return GetRegistry()[name]->GetColor();
        return ImColor(45, 55, 65, 255);
    }

    // --- LA DÉLÉGATION DE LA COMPILATION ---
    static void GenerateNodeGLSL(const MaterialNode& node, std::stringstream& bodyBuilder, const std::function<std::string(int, const std::string&)>& evaluateInput) {
        if (GetRegistry().find(node.Name) != GetRegistry().end()) {
            GetRegistry()[node.Name]->GenerateGLSL(node, bodyBuilder, evaluateInput);
        }
    }
};