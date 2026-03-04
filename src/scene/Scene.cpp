#include "Scene.h"
#include "Entity.h"
#include "../ecs/Components.h"
#include "physics/PhysicsEngine.h"

Scene::Scene() {}
Scene::~Scene() {}



Entity Scene::CreateEntity(const std::string& name) {
    return CreateEntityWithUUID(UUID(), name); // Génère un nouvel UUID par défaut
}

Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name) {
    Entity entity = { m_Registry.create(), this };

    entity.AddComponent<IDComponent>(uuid); // <-- L'identité de base
    entity.AddComponent<TransformComponent>();
    auto& tag = entity.AddComponent<TagComponent>();
    tag.Tag = name.empty() ? "Entity" : name;

    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    if (!entity) return;

    // 1. Détacher l'entité de la hiérarchie (Réparer l'arbre)
    if (entity.HasComponent<RelationshipComponent>()) {
        auto& rel = entity.GetComponent<RelationshipComponent>();

        // Mettre à jour le parent pour qu'il "oublie" cet enfant
        if (rel.Parent != entt::null) {
            Entity parent{ rel.Parent, this };
            auto& parentRel = parent.GetComponent<RelationshipComponent>();
            if (parentRel.FirstChild == entity) {
                parentRel.FirstChild = rel.NextSibling;
            }
        }

        // Mettre à jour les frères pour recoudre la liste
        if (rel.PreviousSibling != entt::null) {
            Entity prev{ rel.PreviousSibling, this };
            prev.GetComponent<RelationshipComponent>().NextSibling = rel.NextSibling;
        }
        if (rel.NextSibling != entt::null) {
            Entity next{ rel.NextSibling, this };
            next.GetComponent<RelationshipComponent>().PreviousSibling = rel.PreviousSibling;
        }

        // 2. Détruire tous les enfants récursivement !
        entt::entity childID = rel.FirstChild;
        while (childID != entt::null) {
            Entity child{ childID, this };
            entt::entity nextSibling = child.GetComponent<RelationshipComponent>().NextSibling;
            DestroyEntity(child); // Récursion mortelle
            childID = nextSibling;
        }
    }

    // 3. Enfin, on détruit la coquille vide dans EnTT
    m_Registry.destroy(entity);
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
            nsc.Instance->m_Node = Entity{ entity, this };

            // On déclenche le "BeginPlay"
            nsc.Instance->OnCreate();
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

void Scene::OnUpdateScripts(float ts) {
    auto view = m_Registry.view<NativeScriptComponent>();
    for (auto entity : view) {
        auto& nsc = view.get<NativeScriptComponent>(entity);
        if (nsc.Instance) {
            nsc.Instance->OnUpdate(ts);
        }
    }
}

void Scene::ParentEntity(Entity entity, Entity parent) {
    // 1. On s'assure que les deux ont le composant
    if (!entity.HasComponent<RelationshipComponent>()) entity.AddComponent<RelationshipComponent>();
    if (!parent.HasComponent<RelationshipComponent>()) parent.AddComponent<RelationshipComponent>();

    auto& rel = entity.GetComponent<RelationshipComponent>();
    auto& parentRel = parent.GetComponent<RelationshipComponent>();

    // 2. On définit le parent
    rel.Parent = parent;

    // 3. On l'ajoute à la liste des enfants du parent
    if (parentRel.FirstChild == entt::null) {
        // C'est le tout premier enfant !
        parentRel.FirstChild = entity;
    } else {
        // Il a déjà des enfants, on cherche le dernier "frère" de la liste
        entt::entity current = parentRel.FirstChild;
        auto* currentRel = &m_Registry.get<RelationshipComponent>(current);

        while (currentRel->NextSibling != entt::null) {
            current = currentRel->NextSibling;
            currentRel = &m_Registry.get<RelationshipComponent>(current);
        }

        // On attache notre entité à la fin de la fratrie
        currentRel->NextSibling = entity;
        rel.PreviousSibling = current;
    }
}

glm::mat4 Scene::GetWorldTransform(Entity entity) {
    glm::mat4 transform(1.0f);

    // Matrice locale
    if (entity.HasComponent<TransformComponent>()) {
        auto& tc = entity.GetComponent<TransformComponent>();
        transform = glm::translate(glm::mat4(1.0f), tc.Location) * glm::toMat4(tc.Rotation) * glm::scale(glm::mat4(1.0f), tc.Scale);
    }

    // Si on a un parent, on multiplie notre matrice locale par la matrice globale du parent (Magie de l'algèbre !)
    if (entity.HasComponent<RelationshipComponent>()) {
        entt::entity parentID = entity.GetComponent<RelationshipComponent>().Parent;
        if (parentID != entt::null) {
            Entity parent{parentID, this};
            transform = GetWorldTransform(parent) * transform; // Appel récursif !
        }
    }

    return transform;
}
