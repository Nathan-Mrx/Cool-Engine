#pragma once
#include "../../scene/Scene.h"
#include "../../scene/Entity.h"
#include <memory>

#include "editor/materials/MaterialNodeRegistry.h"

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
    // --- NOUVELLES SOUS-FONCTIONS DE RENDU (Refactoring) ---
    void DrawHierarchyWindow();
    void DrawInspectorWindow();

    // -- Logique Hierarchy --
    void HandleHierarchyShortcuts();
    void HandleHierarchyEmptySpaceDragDrop();
    void DrawHierarchyContextMenu();
    void DrawEntityNode(Entity entity);

    // -- Logique Entity Node (Intérieur de la boucle) --
    bool DrawEntityNodeRenaming(Entity entity, ImGuiTreeNodeFlags flags, uint32_t entityID);
    void DrawEntityNodeContextMenu(Entity entity, bool hasScript, bool isPrefab);
    void HandleEntityNodeDragDrop(Entity entity);

    // -- Logique Inspector --
    void DrawComponents(Entity entity);
    void DrawMiniHierarchy(Entity node);

private:
    std::shared_ptr<Scene> m_Context;
    Entity m_SelectionContext;

    Entity m_EntityToDestroy = {};

    bool m_IsPrefabScene = false;

    bool m_IsRenaming = false;
    Entity m_RenamingEntity = {};
    char m_RenameBuffer[256] = "";
};
