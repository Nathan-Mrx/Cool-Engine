#pragma once

#include "Scene.h"
#include "ecs/Components.h"
#include "renderer/PrimitiveFactory.h"

#define CE_NODE(Name, Category)

// =====================================================================
// DÉFINITIONS DES NOEUDS (La "Toolbox" du moteur)
// =====================================================================

CE_NODE("Empty Node", "")
struct EmptyNode {
    static void Setup(Entity entity) {}
};

CE_NODE("Camera", "")
struct CameraNode {
    static void Setup(Entity entity) {
        // Le Transform est déjà là, on ajoute juste la caméra
        entity.AddComponent<CameraComponent>();
    }
};

CE_NODE("Cube", "3D Object")
struct CubeNode {
    static void Setup(Entity entity) {
        entity.AddComponent<ColorComponent>(glm::vec3(0.8f));
        auto& mesh = entity.AddComponent<MeshComponent>();
        mesh.MeshData = PrimitiveFactory::CreateCube();
        mesh.AssetPath = "Primitive::Cube";
    }
};

CE_NODE("Sphere", "3D Object")
struct SphereNode {
    static void Setup(Entity entity) {
        entity.AddComponent<ColorComponent>(glm::vec3(0.8f));
        auto& mesh = entity.AddComponent<MeshComponent>();
        mesh.MeshData = PrimitiveFactory::CreateSphere();
        mesh.AssetPath = "Primitive::Sphere";
    }
};

CE_NODE("Plane", "3D Object")
struct PlaneNode {
    static void Setup(Entity entity) {
        entity.AddComponent<ColorComponent>(glm::vec3(0.8f));
        auto& mesh = entity.AddComponent<MeshComponent>();
        mesh.MeshData = PrimitiveFactory::CreatePlane();
        mesh.AssetPath = "Primitive::Plane";
    }
};

CE_NODE("Directional Light", "Light")
struct DirectionalLightNode {
    static void Setup(Entity entity) {
        entity.AddComponent<DirectionalLightComponent>();
    }
};

CE_NODE("Point Light", "Light")
struct PointLightNode {
    static void Setup(Entity entity) {
        // entity.AddComponent<PointLightComponent>();
    }
};

CE_NODE("Skybox", "Environment")
struct SkyboxNode {
    static void Setup(Entity entity) {
        entity.AddComponent<SkyboxComponent>();
    }
};