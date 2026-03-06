#pragma once
#include "Scene.h"

class Entity {
public:
    Entity() = default;
    Entity(const entt::entity handle, Scene* scene) : m_EntityHandle(handle), m_Scene(scene) {}

    [[nodiscard]] UUID GetUUID();

    // Méthodes templates INDISPENSABLES dans le header
    template<typename T, typename... Args>
    T& AddComponent(Args&&... args) {
        return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
    }

    template<typename T>
    T& GetComponent() { return m_Scene->m_Registry.get<T>(m_EntityHandle); }

    template<typename T>
    [[nodiscard]] bool HasComponent() const { return m_Scene->m_Registry.all_of<T>(m_EntityHandle); }

    template<typename T>
    void RemoveComponent() const
    {
        m_Scene->m_Registry.remove<T>(m_EntityHandle);
    }

    [[nodiscard]] Scene* GetScene() const { return m_Scene; }

    operator bool() const { return m_EntityHandle != entt::null; }
    operator entt::entity() const { return m_EntityHandle; }
    bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }

private:
    entt::entity m_EntityHandle{ entt::null };
    Scene* m_Scene = nullptr;
};

using Node = Entity;