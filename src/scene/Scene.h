#pragma once
#include <entt/entt.hpp>
#include <string>
#include <glm/fwd.hpp>

#include "core/UUID.h"

class Entity;

class Scene {
public:
    Scene();
    ~Scene();

    Entity CreateEntity(const std::string& name = std::string());
    Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());

    void DestroyEntity(Entity entity);
    Entity GetEntityByUUID(UUID uuid);

    entt::registry m_Registry; // Accès direct pour le Renderer

    void OnPhysicsStart();
    void OnPhysicsStop();
    void OnUpdatePhysics(float ts);

    void OnScriptStart();
    void OnScriptStop();
    void OnUpdateScripts(float ts);

    // Gestion de la hiérarchie
    void ParentEntity(Entity entity, Entity parent);
    glm::mat4 GetWorldTransform(Entity entity);

private:
    friend class Entity;
};