#include "SceneHierarchyPanel.h"
#include "../../ecs/Components.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

#include "editor/EditorCommands.h"
#include "editor/UndoManager.h"
#include "renderer/PrimitiveFactory.h"
#include "scene/SceneSerializer.h"
#include "scripts/ScriptRegistry.h"
#include "scene/NodeRegistry.generated.h"

// =========================================================================================
// SYSTEME DE PROPRIÉTÉS (Property Drawer)
// =========================================================================================

template<typename Comp, typename FieldType, typename DrawFunc>
static void DrawUndoableProperty(const std::string& label, Entity entity, const std::shared_ptr<Scene>& sceneContext, Comp& component, FieldType& field, DrawFunc drawFunc) {
    static Comp s_StartComp;
    static bool s_IsDragging = false;

    if (!s_IsDragging) s_StartComp = component;

    drawFunc(label.c_str(), field);

    if (ImGui::IsItemActivated()) s_IsDragging = true;

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s_IsDragging = false;
        UndoManager::BeginTransaction("Edit " + label);
        UndoManager::PushAction(std::make_unique<EntityComponentCommand<Comp>>(sceneContext, entity.GetUUID(), s_StartComp, component));
        UndoManager::EndTransaction();
    } else if (ImGui::IsItemDeactivated()) {
        s_IsDragging = false;
    }
}

// -----------------------------------------------------------------------------------------
// Déclaration générique (Fallback de sécurité si un composant manque de spécialisation)
template<typename T>
void DrawComponentUI(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<T>()) {
        const char* name = GetComponentName<T>();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;

        if (ImGui::TreeNodeEx((void*)typeid(T).hash_code(), flags, "%s", name)) {
            ImGui::TextDisabled("UI non definie pour ce composant.");
            ImGui::TreePop();
        }
    }
}

// --- LES COMPOSANTS INVISIBLES (Ignorés dans la boucle standard) ---
template<> void DrawComponentUI<TagComponent>(Entity entity, const std::shared_ptr<Scene>& context) {}
template<> void DrawComponentUI<IDComponent>(Entity entity, const std::shared_ptr<Scene>& context) {}
template<> void DrawComponentUI<RelationshipComponent>(Entity entity, const std::shared_ptr<Scene>& context) {}
template<> void DrawComponentUI<PrefabComponent>(Entity entity, const std::shared_ptr<Scene>& context) {}

// --- SPÉCIALISATIONS AAA ---

template<>
void DrawComponentUI<TransformComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<TransformComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), flags, "Transform")) {
            auto& tc = entity.GetComponent<TransformComponent>();

            DrawUndoableProperty("Location", entity, context, tc, tc.Location, [](const char* l, glm::vec3& v) { ImGui::DragFloat3(l, glm::value_ptr(v), 0.1f); });
            DrawUndoableProperty("Rotation", entity, context, tc, tc.RotationEuler, [](const char* l, glm::vec3& v) { ImGui::DragFloat3(l, glm::value_ptr(v), 0.1f); });
            tc.Rotation = glm::quat(glm::radians(tc.RotationEuler));
            DrawUndoableProperty("Scale", entity, context, tc, tc.Scale, [](const char* l, glm::vec3& v) { ImGui::DragFloat3(l, glm::value_ptr(v), 0.1f); });

            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<ColorComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<ColorComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(ColorComponent).hash_code(), flags, "Color")) {
            auto& cc = entity.GetComponent<ColorComponent>();
            DrawUndoableProperty("Albedo", entity, context, cc, cc.Color, [](const char* l, glm::vec3& v) { ImGui::ColorEdit3(l, glm::value_ptr(v)); });
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<CameraComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<CameraComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), flags, "Camera")) {
            auto& cc = entity.GetComponent<CameraComponent>();
            DrawUndoableProperty("Primary", entity, context, cc, cc.Primary, [](const char* l, bool& v) { ImGui::Checkbox(l, &v); });
            DrawUndoableProperty("FOV", entity, context, cc, cc.FOV, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.5f, 10.0f, 150.0f); });
            DrawUndoableProperty("Near Clip", entity, context, cc, cc.NearClip, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.1f, 0.01f, 100.0f); });
            DrawUndoableProperty("Far Clip", entity, context, cc, cc.FarClip, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 100.0f, 100.0f, 1000000.0f); });
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<DirectionalLightComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<DirectionalLightComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(DirectionalLightComponent).hash_code(), flags, "Directional Light")) {
            auto& dlc = entity.GetComponent<DirectionalLightComponent>();
            DrawUndoableProperty("Color", entity, context, dlc, dlc.Color, [](const char* l, glm::vec3& v) { ImGui::ColorEdit3(l, glm::value_ptr(v)); });
            DrawUndoableProperty("Ambient", entity, context, dlc, dlc.AmbientIntensity, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.01f, 0.0f, 1.0f); });
            DrawUndoableProperty("Diffuse", entity, context, dlc, dlc.DiffuseIntensity, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.01f, 0.0f, 1.0f); });
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<RigidBodyComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<RigidBodyComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(RigidBodyComponent).hash_code(), flags, "Rigid Body")) {
            auto& rb = entity.GetComponent<RigidBodyComponent>();

            const char* bodyTypeStrings[] = { "Static", "Kinematic", "Dynamic" };
            const char* currentBodyTypeString = bodyTypeStrings[(int)rb.Type];

            if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
                for (int i = 0; i < 3; i++) {
                    bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                    if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                        RigidBodyComponent before = rb;
                        rb.Type = (RigidBodyType)i;

                        UndoManager::BeginTransaction("Change Body Type");
                        UndoManager::PushAction(std::make_unique<EntityComponentCommand<RigidBodyComponent>>(context, entity.GetUUID(), before, rb));
                        UndoManager::EndTransaction();
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (rb.Type == RigidBodyType::Dynamic) {
                DrawUndoableProperty("Mass", entity, context, rb, rb.Mass, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.1f, 0.01f, 10000.0f); });
            }
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<BoxColliderComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<BoxColliderComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(BoxColliderComponent).hash_code(), flags, "Box Collider")) {
            auto& bc = entity.GetComponent<BoxColliderComponent>();

            glm::vec3 visualSize = bc.HalfSize * 2.0f;
            DrawUndoableProperty("Size", entity, context, bc, visualSize, [](const char* l, glm::vec3& v) { ImGui::DragFloat3(l, glm::value_ptr(v), 0.1f); });
            bc.HalfSize = visualSize / 2.0f;

            DrawUndoableProperty("Offset", entity, context, bc, bc.Offset, [](const char* l, glm::vec3& v) { ImGui::DragFloat3(l, glm::value_ptr(v), 0.1f); });
            DrawUndoableProperty("Friction", entity, context, bc, bc.Friction, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.01f, 0.0f, 1.0f); });
            DrawUndoableProperty("Restitution", entity, context, bc, bc.Restitution, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.01f, 0.0f, 1.0f); });
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<MeshComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<MeshComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(MeshComponent).hash_code(), flags, "Mesh")) {
            auto& mesh = entity.GetComponent<MeshComponent>();

            if (mesh.MeshData) ImGui::TextWrapped("Path: %s", mesh.AssetPath.c_str());
            else ImGui::TextColored(ImVec4(1, 0, 0, 1), "No Mesh Assigned");

            ImGui::Spacing();
            ImGui::Button("Drop .obj Here to Load", ImVec2(-1, 40));

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::filesystem::path filepath = (const char*)payload->Data;
                    if (filepath.extension() == ".obj" || filepath.extension() == ".fbx") {
                        MeshComponent before = mesh;
                        mesh.AssetPath = filepath.string();
                        mesh.MeshData = ModelLoader::LoadModel(mesh.AssetPath);

                        UndoManager::BeginTransaction("Assign Mesh");
                        UndoManager::PushAction(std::make_unique<EntityComponentCommand<MeshComponent>>(context, entity.GetUUID(), before, mesh));
                        UndoManager::EndTransaction();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<MaterialComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<MaterialComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(MaterialComponent).hash_code(), flags, "Material")) {
            auto& mat = entity.GetComponent<MaterialComponent>();

            if (!mat.AssetPath.empty()) ImGui::TextWrapped("Mat: %s", std::filesystem::path(mat.AssetPath).filename().string().c_str());
            else ImGui::TextColored(ImVec4(1, 0, 0, 1), "No Material Assigned");

            ImGui::Button("Drop .cemat/.cematinst", ImVec2(-1, 30));

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::filesystem::path filepath = (const char*)payload->Data;
                    if (filepath.extension() == ".cemat" || filepath.extension() == ".cematinst") {
                        MaterialComponent before = mat;
                        mat.SetAndCompile(filepath.string());

                        UndoManager::BeginTransaction("Assign Material");
                        UndoManager::PushAction(std::make_unique<EntityComponentCommand<MaterialComponent>>(context, entity.GetUUID(), before, mat));
                        UndoManager::EndTransaction();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<NativeScriptComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<NativeScriptComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(NativeScriptComponent).hash_code(), flags, "Native Script")) {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            ImGui::Text("Script: %s", nsc.ScriptName.c_str());
            ImGui::TreePop();
        }
    }
}

template<>
void DrawComponentUI<SkyboxComponent>(Entity entity, const std::shared_ptr<Scene>& context) {
    if (entity.HasComponent<SkyboxComponent>()) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (ImGui::TreeNodeEx((void*)typeid(SkyboxComponent).hash_code(), flags, "Skybox")) {
            auto& skybox = entity.GetComponent<SkyboxComponent>();

            // 1. Le chemin du fichier (Avec Drag & Drop)
            if (!skybox.HDRPath.empty()) {
                ImGui::TextWrapped("HDR: %s", std::filesystem::path(skybox.HDRPath).filename().string().c_str());
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "No HDR Assigned");
            }

            ImGui::Button("Drop .hdr Here", ImVec2(-1, 30));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::filesystem::path filepath = (const char*)payload->Data;
                    if (filepath.extension() == ".hdr") {
                        SkyboxComponent before = skybox;
                        skybox.HDRPath = filepath.string();

                        UndoManager::BeginTransaction("Assign Skybox HDR");
                        UndoManager::PushAction(std::make_unique<EntityComponentCommand<SkyboxComponent>>(context, entity.GetUUID(), before, skybox));
                        UndoManager::EndTransaction();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Spacing();

            // 2. Les réglages
            DrawUndoableProperty("Intensity", entity, context, skybox, skybox.Intensity, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 0.05f, 0.0f, 10.0f); });
            DrawUndoableProperty("Rotation", entity, context, skybox, skybox.Rotation, [](const char* l, float& v) { ImGui::DragFloat(l, &v, 1.0f, 0.0f, 360.0f); });

            ImGui::TreePop();
        }
    }
}

// =========================================================================================
// ENTRY POINT DU RENDU UI
// =========================================================================================
void SceneHierarchyPanel::OnImGuiRender() {
    DrawHierarchyWindow();
    DrawInspectorWindow();

    if (m_EntityToDestroy) {
        m_Context->DestroyEntity(m_EntityToDestroy);
        if (m_SelectionContext == m_EntityToDestroy) m_SelectionContext = {};
        m_EntityToDestroy = {};
    }
}

// =========================================================================================
// 1. FENÊTRE HIÉRARCHIE (Liste des entités)
// =========================================================================================
void SceneHierarchyPanel::DrawHierarchyWindow() {
    ImGui::Begin("Scene Hierarchy");

    HandleHierarchyShortcuts();

    auto view = m_Context->m_Registry.view<TagComponent>();
    for (auto entityID : view) {
        Entity entity{ entityID, m_Context.get() };

        bool isRoot = true;
        if (entity.HasComponent<RelationshipComponent>()) {
            if (entity.GetComponent<RelationshipComponent>().Parent != entt::null) {
                isRoot = false;
            }
        }

        if (isRoot) {
            DrawEntityNode(entity);
        }
    }

    HandleHierarchyEmptySpaceDragDrop();
    DrawHierarchyContextMenu();

    ImGui::End();
}

void SceneHierarchyPanel::HandleHierarchyShortcuts() {
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2) && m_SelectionContext) {
        m_IsRenaming = true;
        m_RenamingEntity = m_SelectionContext;
        snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", m_SelectionContext.GetComponent<TagComponent>().Tag.c_str());
    }
    // Raccourci de Suppression (Le Undo est déjà géré dans OnImGuiRender !)
    if (ImGui::IsWindowFocused() && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && m_SelectionContext) {
        m_EntityToDestroy = m_SelectionContext;
    }
}

void SceneHierarchyPanel::HandleHierarchyEmptySpaceDragDrop() {
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget()) {
        // Dé-parenter une entité (La ramener à la racine)
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
            uint64_t droppedUUID = *(const uint64_t*)payload->Data;
            Entity droppedEntity = m_Context->GetEntityByUUID(droppedUUID);
            if (droppedEntity && droppedEntity.HasComponent<RelationshipComponent>() && droppedEntity.GetComponent<RelationshipComponent>().Parent != entt::null) {
                uint64_t oldParentUUID = Entity{droppedEntity.GetComponent<RelationshipComponent>().Parent, m_Context.get()}.GetUUID();

                m_Context->UnparentEntity(droppedEntity);

                UndoManager::BeginTransaction("Unparent Node");
                UndoManager::PushAction(std::make_unique<EntityReparentCommand>(m_Context, droppedEntity.GetUUID(), oldParentUUID, 0));
                UndoManager::EndTransaction();
            }
        }
        // Lâcher un Prefab à la racine
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::filesystem::path filepath = (const char*)payload->Data;
            if (filepath.extension() == ".ceprefab") {
                SceneSerializer serializer(m_Context);
                Entity prefabRoot = serializer.DeserializePrefab(filepath.string());
                if (prefabRoot) {
                    UndoManager::BeginTransaction("Instantiate Prefab");
                    UndoManager::PushAction(std::make_unique<EntityLifecycleCommand>(m_Context, EntityLifecycleCommand::ActionType::Create, serializer.SerializeEntity(prefabRoot)));
                    UndoManager::EndTransaction();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void SceneHierarchyPanel::DrawHierarchyContextMenu() {
    if (ImGui::BeginPopupContextWindow("SceneHierarchyContextWindow", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

        auto autoParentToPrefabRoot = [&](Entity newEntity) {
            if (m_IsPrefabScene) {
                Entity root = {};
                auto view = m_Context->m_Registry.view<TagComponent>();
                for (auto entityID : view) {
                    Entity e{ entityID, m_Context.get() };
                    if (e != newEntity && (!e.HasComponent<RelationshipComponent>() || e.GetComponent<RelationshipComponent>().Parent == entt::null)) {
                        root = e;
                        break;
                    }
                }
                if (root) m_Context->ParentEntity(newEntity, root);
            }
        };

        // Fonction locale pour créer le noeud et gérer l'Undo
        auto createNode = [&](const NodeRegistryEntry& node) {
            Entity newEntity = m_Context->CreateEntity(node.Name);
            node.SetupFunc(newEntity);
            autoParentToPrefabRoot(newEntity);

            // Enregistrement de la création pour le Undo
            SceneSerializer serializer(m_Context);
            UndoManager::BeginTransaction("Create " + node.Name);
            UndoManager::PushAction(std::make_unique<EntityLifecycleCommand>(
                m_Context, EntityLifecycleCommand::ActionType::Create, serializer.SerializeEntity(newEntity)
            ));
            UndoManager::EndTransaction();
        };

        // 1. On affiche d'abord les noeuds sans catégorie (Catégorie "")
        if (NodeRegistry::Categories.find("") != NodeRegistry::Categories.end()) {
            for (const auto& node : NodeRegistry::Categories[""]) {
                if (ImGui::MenuItem(node.Name.c_str())) createNode(node);
            }
            ImGui::Separator();
        }

        // 2. Ensuite on affiche toutes les catégories sous forme de sous-menus
        for (const auto& [category, nodes] : NodeRegistry::Categories) {
            if (category.empty()) continue; // Déjà géré

            if (ImGui::BeginMenu(category.c_str())) {
                for (const auto& node : nodes) {
                    if (ImGui::MenuItem(node.Name.c_str())) createNode(node);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::EndPopup();
    }
}

// =========================================================================================
// LOGIQUE D'UN NOEUD (Entité)
// =========================================================================================
void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
    uint32_t entityID = (uint32_t)(entt::entity)entity;
    ImGui::PushID(entityID);

    auto& tag = entity.GetComponent<TagComponent>().Tag;
    bool hasScript = entity.HasComponent<NativeScriptComponent>();
    bool isPrefab = entity.HasComponent<PrefabComponent>();
    bool hasChildren = entity.HasComponent<RelationshipComponent>() && entity.GetComponent<RelationshipComponent>().FirstChild != entt::null;

    bool isSelected = (m_SelectionContext == entity) || (GetPrefabRoot(m_SelectionContext) == entity);
    ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (isPrefab || !hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    if (isPrefab) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));

    bool opened = false;

    if (m_IsRenaming && m_RenamingEntity == entity) {
        opened = DrawEntityNodeRenaming(entity, flags, entityID);
    } else {
        opened = ImGui::TreeNodeEx((void*)(uintptr_t)entityID, flags, "%s%s", tag.c_str(), hasScript ? " [S]" : "");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (m_SelectionContext == entity && !ImGui::IsItemToggledOpen()) {
                m_IsRenaming = true;
                m_RenamingEntity = entity;
                snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", tag.c_str());
            } else {
                m_SelectionContext = entity;
                m_IsRenaming = false;
            }
        }
    }

    if (isPrefab) ImGui::PopStyleColor();

    HandleEntityNodeDragDrop(entity);
    DrawEntityNodeContextMenu(entity, hasScript, isPrefab);

    if (opened) {
        if (!isPrefab && hasChildren) {
            entt::entity childID = entity.GetComponent<RelationshipComponent>().FirstChild;
            while (childID != entt::null) {
                Entity child{childID, m_Context.get()};
                entt::entity nextSibling = child.GetComponent<RelationshipComponent>().NextSibling;
                DrawEntityNode(child);
                childID = nextSibling;
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

bool SceneHierarchyPanel::DrawEntityNodeRenaming(Entity entity, ImGuiTreeNodeFlags flags, uint32_t entityID) {
    bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entityID, flags, "");
    ImGui::SameLine();
    ImGui::PushItemWidth(-1);

    if (ImGui::InputText("##RenameInput", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        if (strlen(m_RenameBuffer) > 0) entity.GetComponent<TagComponent>().Tag = m_RenameBuffer;
        m_IsRenaming = false;
    }

    if (!ImGui::IsItemActive() && ImGui::IsWindowFocused()) ImGui::SetKeyboardFocusHere(-1);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered()) m_IsRenaming = false;

    ImGui::PopItemWidth();
    return opened;
}

void SceneHierarchyPanel::HandleEntityNodeDragDrop(Entity entity) {
    // 1. Source : Permet de glisser cette entité
    if (ImGui::BeginDragDropSource()) {
        uint64_t uuid = entity.GetUUID();
        ImGui::SetDragDropPayload("SCENE_HIERARCHY_ENTITY", &uuid, sizeof(uint64_t));
        ImGui::Text("%s", entity.GetComponent<TagComponent>().Tag.c_str());
        ImGui::EndDragDropSource();
    }

    // 2. Cible : Permet de lâcher quelque chose SUR cette entité
    if (ImGui::BeginDragDropTarget()) {
        // A. Lâcher une autre entité pour la parenter
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
            uint64_t droppedUUID = *(const uint64_t*)payload->Data;
            Entity droppedEntity = m_Context->GetEntityByUUID(droppedUUID);

            if (droppedEntity && droppedEntity != entity) {
                uint64_t oldParentUUID = 0;
                if (droppedEntity.HasComponent<RelationshipComponent>()) {
                    entt::entity pID = droppedEntity.GetComponent<RelationshipComponent>().Parent;
                    if (pID != entt::null) oldParentUUID = Entity{pID, m_Context.get()}.GetUUID();
                }

                m_Context->ParentEntity(droppedEntity, entity);

                UndoManager::BeginTransaction("Reparent Node");
                UndoManager::PushAction(std::make_unique<EntityReparentCommand>(m_Context, droppedEntity.GetUUID(), oldParentUUID, entity.GetUUID()));
                UndoManager::EndTransaction();
            }
        }

        // B. Lâcher un Prefab pour l'instancier en tant qu'enfant ! (UNDO AJOUTÉ)
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::filesystem::path filepath = (const char*)payload->Data;
            if (filepath.extension() == ".ceprefab") {
                SceneSerializer serializer(m_Context);
                Entity prefabRoot = serializer.DeserializePrefab(filepath.string());
                if (prefabRoot) {
                    m_Context->ParentEntity(prefabRoot, entity);

                    UndoManager::BeginTransaction("Instantiate Prefab");
                    UndoManager::PushAction(std::make_unique<EntityLifecycleCommand>(m_Context, EntityLifecycleCommand::ActionType::Create, serializer.SerializeEntity(prefabRoot)));
                    UndoManager::EndTransaction();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void SceneHierarchyPanel::DrawEntityNodeContextMenu(Entity entity, bool hasScript, bool isPrefab) {
    if (ImGui::BeginPopupContextItem()) {

        // --- SECTION SCRIPT ---
        if (!hasScript) {
            if (ImGui::BeginMenu("Attach Script...")) {
                for (auto const& [name, func] : ScriptRegistry::Registry) {
                    if (ImGui::MenuItem(name.c_str())) {
                        entity.AddComponent<NativeScriptComponent>();
                        auto& nsc = entity.GetComponent<NativeScriptComponent>();
                        func(nsc);
                        nsc.ScriptName = name;
                    }
                }
                ImGui::EndMenu();
            }
        } else {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            ImGui::TextDisabled("Script: %s", nsc.ScriptName.c_str());
            if (ImGui::MenuItem("Detach Script")) entity.RemoveComponent<NativeScriptComponent>();
        }

        ImGui::Separator();

        // --- SECTION CREATE CHILD (Dynamique via le CHT !) ---
        if (!isPrefab) {
            if (ImGui::BeginMenu("Create Child")) {

                // Fonction locale pour créer l'enfant et gérer l'Undo
                auto createChildNode = [&](const NodeRegistryEntry& node) {
                    Entity child = m_Context->CreateEntity(node.Name);
                    node.SetupFunc(child);
                    m_Context->ParentEntity(child, entity); // On parente directement à l'entité sur laquelle on a fait clic-droit

                    // Enregistrement pour le Undo global
                    SceneSerializer serializer(m_Context);
                    UndoManager::BeginTransaction("Create Child " + node.Name);
                    UndoManager::PushAction(std::make_unique<EntityLifecycleCommand>(
                        m_Context, EntityLifecycleCommand::ActionType::Create, serializer.SerializeEntity(child)
                    ));
                    UndoManager::EndTransaction();
                };

                // 1. Afficher les noeuds sans catégorie d'abord
                if (NodeRegistry::Categories.find("") != NodeRegistry::Categories.end()) {
                    for (const auto& node : NodeRegistry::Categories[""]) {
                        if (ImGui::MenuItem(node.Name.c_str())) createChildNode(node);
                    }
                    ImGui::Separator();
                }

                // 2. Afficher les catégories sous forme de sous-menus
                for (const auto& [category, nodes] : NodeRegistry::Categories) {
                    if (category.empty()) continue; // Déjà géré au-dessus

                    if (ImGui::BeginMenu(category.c_str())) {
                        for (const auto& node : nodes) {
                            if (ImGui::MenuItem(node.Name.c_str())) createChildNode(node);
                        }
                        ImGui::EndMenu();
                    }
                }

                ImGui::EndMenu();
                ImGui::EndMenu();
            }
        }

        ImGui::Separator();

        // --- SECTION DESTRUCTON ---
        if (ImGui::MenuItem("Delete Node")) m_EntityToDestroy = entity;

        ImGui::EndPopup();
    }
}

// =========================================================================================
// 2. FENÊTRE INSPECTEUR (Détails de l'entité sélectionnée)
// =========================================================================================
void SceneHierarchyPanel::DrawInspectorWindow() {
    ImGui::Begin("Inspector");

    if (m_SelectionContext) {
        if (m_Context->m_Registry.valid((entt::entity)m_SelectionContext)) {
            DrawComponents(m_SelectionContext);
        } else {
            m_SelectionContext = {};
        }
    }

    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
        m_SelectionContext = {};
    }

    ImGui::End();
}

void SceneHierarchyPanel::DrawComponents(Entity entity) {
    Entity prefabRoot = GetPrefabRoot(entity);

    if (prefabRoot) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.5f, 1.0f));
        if (ImGui::CollapsingHeader("Prefab Instance", ImGuiTreeNodeFlags_DefaultOpen)) {

            ImGui::TextDisabled("Source: %s", prefabRoot.GetComponent<PrefabComponent>().PrefabPath.c_str());

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

            if (ImGui::BeginChild("MiniHierarchy", ImVec2(0, 150), true)) {
                ImGui::PushID("PrefabInspector");
                DrawMiniHierarchy(prefabRoot);
                ImGui::PopID();
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // --- LE TAG (Nom de l'entité) TOUT EN HAUT ---
    if (entity.HasComponent<TagComponent>()) {
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, tag.c_str(), sizeof(buffer));

        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
            tag = std::string(buffer);
        }
        ImGui::PopItemWidth();
        ImGui::Spacing();
    }

    // 1. Dessin des composants via la boucle Tuple
    std::apply([&](auto... args) {
        (DrawComponentUI<decltype(args)>(entity, m_Context), ...);
    }, AllComponents{});

}

void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene>& context) {
    m_Context = context;
    m_SelectionContext = {};
}

Entity SceneHierarchyPanel::GetPrefabRoot(Entity entity) {
    if (!entity) return {};

    if (entity.HasComponent<PrefabComponent>()) return entity;

    if (entity.HasComponent<RelationshipComponent>()) {
        entt::entity parentID = entity.GetComponent<RelationshipComponent>().Parent;
        if (parentID != entt::null) {
            Entity parent{parentID, m_Context.get()};
            return GetPrefabRoot(parent);
        }
    }
    return {};
}

void SceneHierarchyPanel::DrawMiniHierarchy(Entity node) {
    auto& tag = node.GetComponent<TagComponent>().Tag;

    ImGuiTreeNodeFlags flags = ((m_SelectionContext == node) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

    bool hasChildren = node.HasComponent<RelationshipComponent>() && node.GetComponent<RelationshipComponent>().FirstChild != entt::null;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    uint32_t entityID = (uint32_t)(entt::entity)node;
    bool opened = ImGui::TreeNodeEx((void*)(uint64_t)entityID, flags, "%s", tag.c_str());

    if (ImGui::IsItemClicked()) m_SelectionContext = node;

    if (opened) {
        if (hasChildren) {
            entt::entity childID = node.GetComponent<RelationshipComponent>().FirstChild;
            while (childID != entt::null) {
                Entity child{childID, m_Context.get()};
                DrawMiniHierarchy(child);
                childID = child.GetComponent<RelationshipComponent>().NextSibling;
            }
        }
        ImGui::TreePop();
    }
}
