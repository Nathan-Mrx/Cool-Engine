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

private:
    void DrawEntityNode(Entity entity);
    void DrawComponents(Entity entity);

private:
    std::shared_ptr<Scene> m_Context;
    Entity m_SelectionContext;
};