#include "ScriptableEntity.h"
#include "../ecs/Components.h"
#include <sstream>
#include <vector>

Entity ScriptableEntity::GetNode(const std::string& path) {
    if (path.empty() || !m_Entity) return {};

    // 1. Découper le chemin (ex: "Voiture/Roue" -> ["Voiture", "Roue"])
    std::stringstream ss(path);
    std::string segment;
    std::vector<std::string> segments;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) {
            segments.push_back(segment);
        }
    }

    Entity currentEntity = m_Entity;

    // 2. Naviguer segment par segment
    for (const auto& seg : segments) {
        
        // --- Cas Spécial : Remonter au Parent ("..") ---
        if (seg == "..") {
            if (currentEntity.HasComponent<RelationshipComponent>()) {
                entt::entity parentID = currentEntity.GetComponent<RelationshipComponent>().Parent;
                if (parentID != entt::null) {
                    currentEntity = Entity{ parentID, currentEntity.GetScene() };
                    continue;
                }
            }
            return {}; // On ne peut pas remonter plus haut !
        }

        // --- Cas Normal : Chercher un Enfant par son Tag ---
        bool found = false;
        if (currentEntity.HasComponent<RelationshipComponent>()) {
            entt::entity childID = currentEntity.GetComponent<RelationshipComponent>().FirstChild;
            
            // On boucle sur tous les enfants du niveau actuel
            while (childID != entt::null) {
                Entity child{ childID, currentEntity.GetScene() };
                
                if (child.HasComponent<TagComponent>() && child.GetComponent<TagComponent>().Tag == seg) {
                    currentEntity = child; // Bingo, on descend d'un étage
                    found = true;
                    break;
                }
                
                childID = child.GetComponent<RelationshipComponent>().NextSibling;
            }
        }

        // Si on n'a pas trouvé l'enfant demandé à cette étape, le chemin est brisé
        if (!found) {
            return {}; 
        }
    }

    // On retourne l'entité finale trouvée au bout du chemin
    return currentEntity;
}