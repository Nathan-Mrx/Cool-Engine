#pragma once
#include "../../scene/Scene.h"
#include "../../scene/Entity.h"
#include <memory>

class SceneHierarchyPanel {
public:
    SceneHierarchyPanel() = default;
    SceneHierarchyPanel(const std::shared_ptr<Scene>& context);

    void SetContext(const std::shared_ptr<Scene>& context);
    void OnImGuiRender();

    Entity GetSelectedEntity() const { return m_SelectionContext; }

    void SetSelectedEntity(Entity entity) { m_SelectionContext = entity; }

    Entity GetPrefabRoot(Entity entity);

    void SetIsPrefabScene(bool isPrefab) { m_IsPrefabScene = isPrefab; }

private:
    void DrawEntityNode(Entity entity);
    void DrawComponents(Entity entity);

    void DrawMiniHierarchy(Entity node);

private:
    std::shared_ptr<Scene> m_Context;
    Entity m_SelectionContext;

    Entity m_EntityToDestroy = {};

    bool m_IsPrefabScene = false;
};