#pragma once

#include "UndoManager.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "core/UUID.h"

// Commande générique ultra-rapide pour modifier n'importe quel composant (Transform, Material, etc.)
template<typename T>
class EntityComponentCommand : public IUndoableAction {
public:
    EntityComponentCommand(std::shared_ptr<Scene> scene, UUID entityID, const T& before, const T& after)
        : m_Scene(std::move(scene)), m_EntityID(entityID), m_Before(before), m_After(after) {}

    void Undo() override {
        // Le weak_ptr garantit qu'on ne fait rien si la scène a été fermée/détruite
        if (auto scene = m_Scene.lock()) {
            Entity entity = scene->GetEntityByUUID(m_EntityID);
            if (entity && entity.HasComponent<T>()) {
                entity.GetComponent<T>() = m_Before;
            }
        }
    }

    void Redo() override {
        if (auto scene = m_Scene.lock()) {
            Entity entity = scene->GetEntityByUUID(m_EntityID);
            if (entity && entity.HasComponent<T>()) {
                entity.GetComponent<T>() = m_After;
            }
        }
    }

private:
    std::weak_ptr<Scene> m_Scene; // <--- Sécurité absolue : pas de pointeur brut !
    UUID m_EntityID;
    T m_Before;
    T m_After;
};