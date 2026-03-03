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

    void OnPhysicsStart();
    void OnPhysicsStop();
    void OnUpdatePhysics(float ts);
private:
    friend class Entity;
};