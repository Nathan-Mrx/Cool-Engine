#include "Scene.h"
#include "Entity.h"
#include "../ecs/Components.h"

Scene::Scene() {}
Scene::~Scene() {}

Entity Scene::CreateEntity(const std::string& name) {
    Entity entity = { m_Registry.create(), this };
    entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
    entity.AddComponent<TransformComponent>();
    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    // On utilise l'opérateur de conversion vers entt::entity défini dans Entity.h
    m_Registry.destroy((entt::entity)entity);
}