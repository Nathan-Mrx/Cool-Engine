#pragma once
#include "MaterialGraph.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <map>

// --- LE TAG C.H.T (Cool Header Tool) ---
// À placer juste au-dessus d'une struct/class pour l'enregistrer dans l'éditeur.
#define CEMAT_NODE()

// --- LE MOULE (Interface) ---
class IMaterialNodeDef {
public:
    virtual ~IMaterialNodeDef() = default;

    virtual std::string GetName() const = 0;
    virtual std::string GetCategory() const { return "General"; }
    virtual ImColor GetColor() const { return ImColor(45, 55, 65, 255); }

    virtual void Initialize(MaterialNode& node, int& nextId) const = 0;
};

// --- LE REGISTRE GLOBAL ---
class MaterialNodeRegistry {
public:
    // Fonction générée automatiquement par le CoolHeaderTool dans le .generated.cpp !
    static void RegisterAllNodes();

    static std::unordered_map<std::string, std::shared_ptr<IMaterialNodeDef>>& GetRegistry() {
        static std::unordered_map<std::string, std::shared_ptr<IMaterialNodeDef>> registry;
        return registry;
    }

    static void Register(std::shared_ptr<IMaterialNodeDef> def) {
        GetRegistry()[def->GetName()] = def;
    }

    // On passe le noeud en référence pour pouvoir retourner un succès/échec
    // On passe le noeud en référence pour pouvoir retourner un succès/échec
    static bool CreateNode(const std::string& name, int& nextId, MaterialNode& outNode) {
        if (GetRegistry().find(name) != GetRegistry().end()) {
            outNode.ID = ed::NodeId(nextId++);
            outNode.Name = name;

            // --- L'AIRBAG MÉMOIRE : On verrouille la taille pour éviter les crashs de strings ! ---
            outNode.Inputs.reserve(16);
            outNode.Outputs.reserve(16);

            GetRegistry()[name]->Initialize(outNode, nextId);
            return true;
        }
        return false;
    }

    static ImColor GetNodeColor(const std::string& name) {
        if (GetRegistry().find(name) != GetRegistry().end()) {
            return GetRegistry()[name]->GetColor();
        }
        return ImColor(45, 55, 65, 255);
    }
};