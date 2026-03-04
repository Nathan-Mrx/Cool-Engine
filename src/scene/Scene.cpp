#include "Scene.h"
#include "Entity.h"
#include "../ecs/Components.h"
#include "physics/PhysicsEngine.h"

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

void Scene::OnPhysicsStart() {
    auto view = m_Registry.view<RigidBodyComponent, TransformComponent>();
    for (auto entity : view) {
        auto [rb, transform] = view.get<RigidBodyComponent, TransformComponent>(entity);

        // On suppose que c'est une boîte si elle a un BoxColliderComponent
        if (m_Registry.all_of<BoxColliderComponent>(entity)) {
            auto& bc = m_Registry.get<BoxColliderComponent>(entity);

            // La taille physique prend en compte le scale global de l'entité
            glm::vec3 worldHalfExtents = bc.HalfSize * transform.Scale;

            rb.RuntimeBodyID = PhysicsEngine::CreateBoxBody(
                transform.Location + bc.Offset,
                transform.Rotation,
                worldHalfExtents,
                (int)rb.Type,
                rb.Mass
            );
        }
    }
}

void Scene::OnPhysicsStop() {
    auto view = m_Registry.view<RigidBodyComponent>();
    for (auto entity : view) {
        auto& rb = view.get<RigidBodyComponent>(entity);
        if (rb.RuntimeBodyID != 0xFFFFFFFF) {
            PhysicsEngine::DestroyBody(rb.RuntimeBodyID);
            rb.RuntimeBodyID = 0xFFFFFFFF; // Remise à zéro propre
        }
    }
}

void Scene::OnUpdatePhysics(float ts) {
    // 1. On avance le temps dans Jolt
    PhysicsEngine::Update(ts);

    // 2. On récupère les nouvelles positions calculées par Jolt pour synchroniser nos Transform
    auto view = m_Registry.view<RigidBodyComponent, TransformComponent>();
    for (auto entity : view) {
        auto [rb, transform] = view.get<RigidBodyComponent, TransformComponent>(entity);

        // On ne met à jour que ce qui peut bouger (Dynamic ou Kinematic)
        if (rb.Type != RigidBodyType::Static && rb.RuntimeBodyID != 0xFFFFFFFF) {
            glm::vec3 newPos;
            glm::quat newRot;
            PhysicsEngine::GetBodyTransform(rb.RuntimeBodyID, newPos, newRot);

            transform.Location = newPos; // Si un offset est utilisé, il faudrait le soustraire ici
            transform.Rotation = newRot;
            transform.RotationEuler = glm::degrees(glm::eulerAngles(newRot)); // MAJ du panneau UI
        }
    }
}

void Scene::OnScriptStart() {
    auto view = m_Registry.view<NativeScriptComponent>();
    for (auto entity : view) {
        auto& nsc = view.get<NativeScriptComponent>(entity);

        // Si un script a bien été lié à cette entité
        if (nsc.InstantiateScript) {
            nsc.Instance = nsc.InstantiateScript();

            // On injecte secrètement l'entité actuelle dans la classe C++
            nsc.Instance->m_Entity = Entity{ entity, this };

            // On déclenche le "BeginPlay"
            nsc.Instance->OnCreate();
        }
    }
}

void Scene::OnUpdateScripts(float ts) {
    auto view = m_Registry.view<NativeScriptComponent>();
    for (auto entity : view) {
        auto& nsc = view.get<NativeScriptComponent>(entity);
        if (nsc.Instance) {
            nsc.Instance->OnUpdate(ts);
        }
    }
}

void Scene::OnScriptStop() {
    auto view = m_Registry.view<NativeScriptComponent>();
    for (auto entity : view) {
        auto& nsc = view.get<NativeScriptComponent>(entity);
        if (nsc.Instance) {
            nsc.Instance->OnDestroy();
            nsc.DestroyScript(&nsc); // Désallocation propre de la mémoire
        }
    }
}