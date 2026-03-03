#pragma once
#include <entt/entt.hpp>
#include <string>

class Entity;

class Scene {
public:
    Scene();
    ~Scene();

    Entity CreateEntity(const std::string& name = std::string());
    void DestroyEntity(Entity entity);

    entt::registry m_Registry; // Accès direct pour le Renderer
private:
    friend class Entity;
};