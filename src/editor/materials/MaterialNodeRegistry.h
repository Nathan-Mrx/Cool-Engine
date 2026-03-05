#pragma once
#include <imgui.h>

#include "MaterialGraph.h" // (Ou le fichier où tu as défini MaterialNode et MaterialPin)
#include <string>
#include <unordered_map>
#include <memory>
#include <map>

// --- LE MOULE (Interface) ---
class IMaterialNodeDef {
public:
    virtual ~IMaterialNodeDef() = default;
    
    virtual std::string GetName() const = 0;
    virtual std::string GetCategory() const { return "General"; } // Pratique pour le menu Clic Droit !
    virtual ImColor GetColor() const { return ImColor(45, 55, 65, 255); }
    
    // C'est ici qu'on définit les pins !
    virtual void Initialize(MaterialNode& node, int& nextId) const = 0;
    
    // (Bonus pour plus tard : On pourra même y ajouter virtual std::string GenerateGLSL() !)
};

// --- LE REGISTRE GLOBAL ---
class MaterialNodeRegistry {
public:
    // Le dictionnaire qui stocke tous les modèles de nœuds
    static std::unordered_map<std::string, std::shared_ptr<IMaterialNodeDef>>& GetRegistry() {
        static std::unordered_map<std::string, std::shared_ptr<IMaterialNodeDef>> registry;
        return registry;
    }

    // Ajouter un nœud au moteur
    static void Register(std::shared_ptr<IMaterialNodeDef> def) {
        GetRegistry()[def->GetName()] = def;
    }

    // Fabriquer un nouveau nœud
    static MaterialNode CreateNode(const std::string& name, int& nextId) {
        MaterialNode node;
        node.ID = ed::NodeId(nextId++);
        node.Name = name;
        
        if (GetRegistry().find(name) != GetRegistry().end()) {
            GetRegistry()[name]->Initialize(node, nextId);
        }
        return node;
    }

    static ImColor GetNodeColor(const std::string& name) {
        if (GetRegistry().find(name) != GetRegistry().end()) {
            return GetRegistry()[name]->GetColor();
        }
        return ImColor(45, 55, 65, 255);
    }
};

// --- LA MACRO MAGIQUE D'AUTO-ENREGISTREMENT ---
// Se place à l'intérieur de la définition du noeud.
#define CEMAT_NODE(ClassName) \
private: \
inline static const bool s_IsRegistered = []() { \
MaterialNodeRegistry::Register(std::make_shared<ClassName>()); \
return true; \
}(); \
public: