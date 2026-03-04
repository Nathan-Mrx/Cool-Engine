#include "SceneHierarchyPanel.h"
#include "../../ecs/Components.h"
#include <imgui.h>
#include <imgui_internal.h>

#include "renderer/PrimitiveFactory.h"
#include "scene/SceneSerializer.h"
#include "scripts/ScriptRegistry.h"

// --- RÉFLEXION UI ---
template <typename T>
const char* GetComponentName() {
    if constexpr (std::is_same_v<T, TagComponent>) return "Tag";
    if constexpr (std::is_same_v<T, TransformComponent>) return "Transform";
    if constexpr (std::is_same_v<T, ColorComponent>) return "Color";
    if constexpr (std::is_same_v<T, CameraComponent>) return "Camera";
    if constexpr (std::is_same_v<T, MeshComponent>) return "Mesh";
    if constexpr (std::is_same_v<T, DirectionalLightComponent>) return "Directional Light";
    //if constexpr (std::is_same_v<T, PointLightComponent>) return "Point Light"; // not implemented yet
    if constexpr (std::is_same_v<T, RigidBodyComponent>) return "Rigid Body";
    if constexpr (std::is_same_v<T, BoxColliderComponent>) return "Box Collider";
    return "Unknown Component";
}

template<typename T>
void DrawComponentUI(Entity entity, entt::registry& registry) {
    if (entity.HasComponent<T>()) {
        const char* name = GetComponentName<T>();

        // Flags pour un look d'éditeur professionnel (ouvert par défaut, prend toute la largeur)
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;

        // On utilise le hash du type comme ID unique pour ImGui
        if (ImGui::TreeNodeEx((void*)typeid(T).hash_code(), flags, "%s", name)) {
            auto& component = entity.GetComponent<T>();

            // Appel magique : exécute l'UI spécifique définie dans Components.h
            component.OnImGuiRender();

            ImGui::TreePop();
        }
    }
}

template<typename T>
void DrawAddComponentEntry(Entity entity) {
    // On n'affiche le composant dans la liste que si l'entité ne l'a pas encore !
    if (!entity.HasComponent<T>()) {
        if (ImGui::MenuItem(GetComponentName<T>())) {
            entity.AddComponent<T>();
            ImGui::CloseCurrentPopup();
        }
    }
}

void SceneHierarchyPanel::OnImGuiRender() {
    ImGui::Begin("Scene Hierarchy");

    // On utilise une vue simple pour l'itération
    auto view = m_Context->m_Registry.view<TagComponent>();
    for (auto entityID : view) {
        Entity entity{ entityID, m_Context.get() };

        // Est-ce un objet racine ?
        bool isRoot = true;
        if (entity.HasComponent<RelationshipComponent>()) {
            if (entity.GetComponent<RelationshipComponent>().Parent != entt::null) {
                isRoot = false; // Il a un parent, on ne le dessine pas ici !
            }
        }

        if (isRoot) {
            DrawEntityNode(entity);
        }
    }

    // --- DRAG & DROP : Instancier un Prefab À LA RACINE ---
    // On crée une zone invisible qui prend tout le reste de la fenêtre
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::filesystem::path filepath = (const char*)payload->Data;
            if (filepath.extension() == ".ceprefab") {
                SceneSerializer serializer(m_Context);
                serializer.DeserializePrefab(filepath.string());
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Le clic droit dans le vide de la hiérarchie
    if (ImGui::BeginPopupContextWindow("SceneHierarchyContextWindow", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

        // --- LAMBDA MAGIQUE : Parentage automatique pour les Prefabs ---
        auto autoParentToPrefabRoot = [&](Entity newEntity) {
            if (m_IsPrefabScene) {
                Entity root = {};
                auto view = m_Context->m_Registry.view<TagComponent>();
                for (auto entityID : view) {
                    Entity e{ entityID, m_Context.get() };
                    if (e != newEntity && (!e.HasComponent<RelationshipComponent>() || e.GetComponent<RelationshipComponent>().Parent == entt::null)) {
                        root = e;
                        break; // On a trouvé la racine, on arrête de chercher !
                    }
                }
                if (root) m_Context->ParentEntity(newEntity, root);
            }
        };

        if (ImGui::MenuItem("Create Empty Entity")) {
            Entity newEntity = m_Context->CreateEntity("Empty Entity");
            autoParentToPrefabRoot(newEntity);
        }

        ImGui::Separator();

        // --- Sous-menu pour les primitives 3D ---
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) {
                auto newEntity = m_Context->CreateEntity("Cube");
                if (!newEntity.HasComponent<TransformComponent>()) newEntity.AddComponent<TransformComponent>();
                newEntity.AddComponent<ColorComponent>(glm::vec3(0.8f));
                auto& mesh = newEntity.AddComponent<MeshComponent>();
                mesh.MeshData = PrimitiveFactory::CreateCube();
                mesh.AssetPath = "Primitive::Cube";
                autoParentToPrefabRoot(newEntity); // <-- Sécurisé !
            }
            if (ImGui::MenuItem("Sphere")) {
                auto newEntity = m_Context->CreateEntity("Sphere");
                if (!newEntity.HasComponent<TransformComponent>()) newEntity.AddComponent<TransformComponent>();
                newEntity.AddComponent<ColorComponent>(glm::vec3(0.8f));
                auto& mesh = newEntity.AddComponent<MeshComponent>();
                mesh.MeshData = PrimitiveFactory::CreateSphere();
                mesh.AssetPath = "Primitive::Sphere";
                autoParentToPrefabRoot(newEntity); // <-- Sécurisé !
            }
            if (ImGui::MenuItem("Plane")) {
                auto newEntity = m_Context->CreateEntity("Plane");
                if (!newEntity.HasComponent<TransformComponent>()) newEntity.AddComponent<TransformComponent>();
                newEntity.AddComponent<ColorComponent>(glm::vec3(0.8f));
                auto& mesh = newEntity.AddComponent<MeshComponent>();
                mesh.MeshData = PrimitiveFactory::CreatePlane();
                mesh.AssetPath = "Primitive::Plane";
                autoParentToPrefabRoot(newEntity); // <-- Sécurisé !
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Camera")) {
            auto newEntity = m_Context->CreateEntity("Camera");
            newEntity.AddComponent<CameraComponent>();
            autoParentToPrefabRoot(newEntity); // <-- Sécurisé !
        }

        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Directional Light")) {
                auto newEntity = m_Context->CreateEntity("Directional Light");
                if (!newEntity.HasComponent<TransformComponent>()) newEntity.AddComponent<TransformComponent>();
                newEntity.AddComponent<DirectionalLightComponent>();
                autoParentToPrefabRoot(newEntity); // <-- Sécurisé !
            }
            if (ImGui::MenuItem("Point Light")) {
                auto newEntity = m_Context->CreateEntity("Point Light");
                autoParentToPrefabRoot(newEntity); // <-- Sécurisé !
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }
    ImGui::End();

    if (m_EntityToDestroy) {
        m_Context->DestroyEntity(m_EntityToDestroy);
        if (m_SelectionContext == m_EntityToDestroy) m_SelectionContext = {};
        m_EntityToDestroy = {};
    }

    // ==========================================
    // FENÊTRE INSPECTOR
    // ==========================================
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

            // On vérifie le retour du BeginChild
            if (ImGui::BeginChild("MiniHierarchy", ImVec2(0, 150), true)) {

                ImGui::PushID("PrefabInspector");
                DrawMiniHierarchy(prefabRoot);
                ImGui::PopID();

            } // Fin du 'if' ICI !

            // --- LE FIX IMGUI ---
            // EndChild doit absolument être en dehors du 'if', il faut l'appeler à chaque frame !
            ImGui::EndChild();

            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // 1. Dessin des composants existants
    std::apply([&](auto... args) {
        (DrawComponentUI<decltype(args)>(entity, m_Context->m_Registry), ...);
    }, AllComponents{});

    // 2. Bouton "Add Component"
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImVec2 buttonSize(150.0f, 30.0f);

    // On centre le bouton mathématiquement
    float cursorX = (ImGui::GetContentRegionAvail().x - buttonSize.x) * 0.5f;
    if (cursorX > 0.0f) {
        ImGui::SetCursorPosX(cursorX);
    }

    if (ImGui::Button("Add Component", buttonSize)) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    // 3. Le Popup (Menu déroulant)
    if (ImGui::BeginPopup("AddComponentPopup")) {
        // On réutilise la magie de la réflexion pour lister tous les composants possibles !
        std::apply([&](auto... args) {
            (DrawAddComponentEntry<decltype(args)>(entity), ...);
        }, AllComponents{});

        ImGui::EndPopup();
    }

}

void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
    auto& tag = entity.GetComponent<TagComponent>().Tag;
    bool hasScript = entity.HasComponent<NativeScriptComponent>();

    // Style Godot : petit indicateur [S]
    std::string displayName = hasScript ? tag + " [S]" : tag;

    // Est-ce que cette entité (ou son parent caché) est sélectionnée ?
    bool isSelected = (m_SelectionContext == entity) || (GetPrefabRoot(m_SelectionContext) == entity);
    ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
    flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

    bool isPrefab = entity.HasComponent<PrefabComponent>();
    bool hasChildren = entity.HasComponent<RelationshipComponent>() && entity.GetComponent<RelationshipComponent>().FirstChild != entt::null;

    // --- LE FIX MAJEUR ---
    // On force le passage par entt::entity pour ne pas tomber dans le piège du booléen !
    uint32_t entityID = (uint32_t)(entt::entity)entity;

    if (isPrefab || !hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // On donne cet ID unique au TreeNode
    if (isPrefab) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
    bool opened = ImGui::TreeNodeEx((void*)(uint64_t)entityID, flags, "%s", displayName.c_str());
    if (isPrefab) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked()) m_SelectionContext = entity;

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::filesystem::path filepath = (const char*)payload->Data;
            if (filepath.extension() == ".ceprefab") {
                SceneSerializer serializer(m_Context);
                Entity prefabRoot = serializer.DeserializePrefab(filepath.string());
                if (prefabRoot) {
                    m_Context->ParentEntity(prefabRoot, entity); // On l'attache à l'entité survolée !
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    bool entityDeleted = false;

    // On utilise BeginPopupContextItem SANS ID.
    // ImGui va automatiquement l'attacher au TreeNode juste au-dessus !
    if (ImGui::BeginPopupContextItem()) {
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
            if (ImGui::MenuItem("Detach Script")) {
                entity.template RemoveComponent<NativeScriptComponent>();
            }
        }

        ImGui::Separator();

        // --- NOUVEAU : Menu complet de création d'enfants ---
        if (!isPrefab) {
            if (ImGui::BeginMenu("Create Child")) {
                if (ImGui::MenuItem("Empty Entity")) {
                    Entity child = m_Context->CreateEntity("Empty Entity");
                    m_Context->ParentEntity(child, entity);
                }

                ImGui::Separator();

                if (ImGui::BeginMenu("3D Object")) {
                    if (ImGui::MenuItem("Cube")) {
                        Entity child = m_Context->CreateEntity("Cube");
                        if (!child.HasComponent<TransformComponent>()) child.AddComponent<TransformComponent>();
                        child.AddComponent<ColorComponent>(glm::vec3(0.8f));
                        auto& mesh = child.AddComponent<MeshComponent>();
                        mesh.MeshData = PrimitiveFactory::CreateCube();
                        mesh.AssetPath = "Primitive::Cube";
                        m_Context->ParentEntity(child, entity);
                    }
                    if (ImGui::MenuItem("Sphere")) {
                        Entity child = m_Context->CreateEntity("Sphere");
                        if (!child.HasComponent<TransformComponent>()) child.AddComponent<TransformComponent>();
                        child.AddComponent<ColorComponent>(glm::vec3(0.8f));
                        auto& mesh = child.AddComponent<MeshComponent>();
                        mesh.MeshData = PrimitiveFactory::CreateSphere();
                        mesh.AssetPath = "Primitive::Sphere";
                        m_Context->ParentEntity(child, entity);
                    }
                    if (ImGui::MenuItem("Plane")) {
                        Entity child = m_Context->CreateEntity("Plane");
                        if (!child.HasComponent<TransformComponent>()) child.AddComponent<TransformComponent>();
                        child.AddComponent<ColorComponent>(glm::vec3(0.8f));
                        auto& mesh = child.AddComponent<MeshComponent>();
                        mesh.MeshData = PrimitiveFactory::CreatePlane();
                        mesh.AssetPath = "Primitive::Plane";
                        m_Context->ParentEntity(child, entity);
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Camera")) {
                    Entity child = m_Context->CreateEntity("Camera");
                    child.AddComponent<CameraComponent>();
                    m_Context->ParentEntity(child, entity);
                }

                if (ImGui::BeginMenu("Light")) {
                    if (ImGui::MenuItem("Directional Light")) {
                        Entity child = m_Context->CreateEntity("Directional Light");
                        if (!child.HasComponent<TransformComponent>()) child.AddComponent<TransformComponent>();
                        child.AddComponent<DirectionalLightComponent>();
                        m_Context->ParentEntity(child, entity);
                    }
                    if (ImGui::MenuItem("Point Light")) {
                        Entity child = m_Context->CreateEntity("Point Light");
                        m_Context->ParentEntity(child, entity);
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }
        } else {
            // Si c'est une instance de prefab, on bloque l'ajout d'enfants sauvages
            ImGui::TextDisabled("Edit Prefab to add children");
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete Entity")) {
            m_EntityToDestroy = entity; // On le marque pour la fin de la frame !
        }
        ImGui::EndPopup();
    }

    if (opened) {
        if (!isPrefab && hasChildren) {
            entt::entity childID = entity.GetComponent<RelationshipComponent>().FirstChild;

            while (childID != entt::null) {
                Entity child{childID, m_Context.get()};

                // --- LE FIX : On pré-récupère le frère avant le dessin ! ---
                entt::entity nextSibling = child.GetComponent<RelationshipComponent>().NextSibling;

                DrawEntityNode(child);

                childID = nextSibling; // On avance prudemment
            }
        }
        ImGui::TreePop();
    }

    // On détruit l'entité en toute sécurité en dehors de la logique d'UI
    if (entityDeleted) {
        m_Context->DestroyEntity(entity);
        if (m_SelectionContext == entity) m_SelectionContext = {};
    }
}

void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene>& context) {
    m_Context = context;
    m_SelectionContext = {}; // On réinitialise la sélection lors du changement de scène
}

Entity SceneHierarchyPanel::GetPrefabRoot(Entity entity) {
    if (!entity) return {};

    // Si l'entité elle-même est la racine
    if (entity.HasComponent<PrefabComponent>()) return entity;

    // Sinon on remonte l'arbre des parents jusqu'à trouver la racine
    if (entity.HasComponent<RelationshipComponent>()) {
        entt::entity parentID = entity.GetComponent<RelationshipComponent>().Parent;
        if (parentID != entt::null) {
            Entity parent{parentID, m_Context.get()};
            return GetPrefabRoot(parent); // Appel récursif
        }
    }
    return {};
}

void SceneHierarchyPanel::DrawMiniHierarchy(Entity node) {
    auto& tag = node.GetComponent<TagComponent>().Tag;

    // Options de l'arbre
    ImGuiTreeNodeFlags flags = ((m_SelectionContext == node) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

    bool hasChildren = node.HasComponent<RelationshipComponent>() && node.GetComponent<RelationshipComponent>().FirstChild != entt::null;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf; // Visuel de feuille

    // --- LE FIX : On évite le piège du booléen ---
    uint32_t entityID = (uint32_t)(entt::entity)node;
    bool opened = ImGui::TreeNodeEx((void*)(uint64_t)entityID, flags, "%s", tag.c_str());

    // Si on clique, ça devient le vrai SelectionContext du moteur !
    if (ImGui::IsItemClicked()) m_SelectionContext = node;

    if (opened) {
        if (hasChildren) {
            entt::entity childID = node.GetComponent<RelationshipComponent>().FirstChild;
            while (childID != entt::null) {
                Entity child{childID, m_Context.get()};
                DrawMiniHierarchy(child); // Rendu récursif
                childID = child.GetComponent<RelationshipComponent>().NextSibling;
            }
        }
        ImGui::TreePop();
    }
}