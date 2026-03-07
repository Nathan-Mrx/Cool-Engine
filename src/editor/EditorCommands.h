#pragma once

#include "UndoManager.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "core/UUID.h"
#include "scene/SceneSerializer.h"
#include <nlohmann/json.hpp>

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

// Commande pour Annuler/Refaire la création ou la suppression d'une entité
class EntityLifecycleCommand : public IUndoableAction {
public:
    enum class ActionType { Create, Destroy };

    EntityLifecycleCommand(std::shared_ptr<Scene> scene, ActionType type, nlohmann::json entityData)
        : m_Scene(std::move(scene)), m_Type(type), m_EntityData(std::move(entityData)) {
        m_EntityID = m_EntityData["Entity"].get<uint64_t>();
    }

    void Undo() override {
        if (auto scene = m_Scene.lock()) {
            if (m_Type == ActionType::Create) {
                // Annuler Création = Détruire
                Entity e = scene->GetEntityByUUID(m_EntityID);
                if (e) scene->DestroyEntity(e);
            } else {
                // Annuler Destruction = Recréer
                SceneSerializer serializer(scene);
                serializer.DeserializeEntity(m_EntityData);
            }
        }
    }

    void Redo() override {
        if (auto scene = m_Scene.lock()) {
            if (m_Type == ActionType::Create) {
                // Refaire Création = Recréer
                SceneSerializer serializer(scene);
                serializer.DeserializeEntity(m_EntityData);
            } else {
                // Refaire Destruction = Détruire
                Entity e = scene->GetEntityByUUID(m_EntityID);
                if (e) scene->DestroyEntity(e);
            }
        }
    }

private:
    std::weak_ptr<Scene> m_Scene;
    ActionType m_Type;
    UUID m_EntityID;
    nlohmann::json m_EntityData;
};

class EntityReparentCommand : public IUndoableAction {
public:
    EntityReparentCommand(std::shared_ptr<Scene> scene, UUID entityID, uint64_t oldParentID, uint64_t newParentID)
        : m_Scene(std::move(scene)), m_EntityID(entityID), m_OldParentID(oldParentID), m_NewParentID(newParentID) {}

    void Undo() override {
        if (auto scene = m_Scene.lock()) {
            Entity entity = scene->GetEntityByUUID(m_EntityID);
            if (!entity) return;

            if (m_OldParentID == 0) scene->UnparentEntity(entity);
            else {
                Entity oldParent = scene->GetEntityByUUID(m_OldParentID);
                if (oldParent) scene->ParentEntity(entity, oldParent);
            }
        }
    }

    void Redo() override {
        if (auto scene = m_Scene.lock()) {
            Entity entity = scene->GetEntityByUUID(m_EntityID);
            if (!entity) return;

            if (m_NewParentID == 0) scene->UnparentEntity(entity);
            else {
                Entity newParent = scene->GetEntityByUUID(m_NewParentID);
                if (newParent) scene->ParentEntity(entity, newParent);
            }
        }
    }

private:
    std::weak_ptr<Scene> m_Scene;
    UUID m_EntityID;
    uint64_t m_OldParentID;
    uint64_t m_NewParentID;
};