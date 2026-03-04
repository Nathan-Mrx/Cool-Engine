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

        if (ImGui::MenuItem("Create Empty Entity")) {
            Entity newEntity = m_Context->CreateEntity("Empty Entity");

            // --- SÉCURITÉ PREFAB : Forcer le parentage ---
            if (m_IsPrefabScene) {
                Entity root = {};

                // FIX : On itère sur toutes les entités via TagComponent au lieu de .each()
                auto view = m_Context->m_Registry.view<TagComponent>();
                for (auto entityID : view) {
                    Entity e{ entityID, m_Context.get() };

                    // On cherche l'entité qui n'a pas de parent (et qui n'est pas celle qu'on vient de créer)
                    if (e != newEntity && (!e.HasComponent<RelationshipComponent>() || e.GetComponent<RelationshipComponent>().Parent == entt::null)) {
                        root = e;
                    }
                }

                if (root) m_Context->ParentEntity(newEntity, root); // Paf ! Attachée de force à la racine !
            }
        }

        ImGui::Separator();

        // --- Sous-menu pour les primitives 3D ---
        // --- Sous-menu pour les primitives 3D ---
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) {
                auto entity = m_Context->CreateEntity("Cube");
                if (!entity.HasComponent<TransformComponent>()) entity.AddComponent<TransformComponent>();
                entity.AddComponent<ColorComponent>(glm::vec3(0.8f));

                auto& mesh = entity.AddComponent<MeshComponent>();
                mesh.MeshData = PrimitiveFactory::CreateCube();
                mesh.AssetPath = "Primitive::Cube"; // Ce tag spécial nous aidera pour la sauvegarde !
            }
            if (ImGui::MenuItem("Sphere")) {
                auto entity = m_Context->CreateEntity("Sphere");
                if (!entity.HasComponent<TransformComponent>()) entity.AddComponent<TransformComponent>();
                entity.AddComponent<ColorComponent>(glm::vec3(0.8f));

                auto& mesh = entity.AddComponent<MeshComponent>();
                mesh.MeshData = PrimitiveFactory::CreateSphere();
                mesh.AssetPath = "Primitive::Sphere";
            }
            if (ImGui::MenuItem("Plane")) {
                auto entity = m_Context->CreateEntity("Plane");
                if (!entity.HasComponent<TransformComponent>()) entity.AddComponent<TransformComponent>();
                entity.AddComponent<ColorComponent>(glm::vec3(0.8f));

                auto& mesh = entity.AddComponent<MeshComponent>();
                mesh.MeshData = PrimitiveFactory::CreatePlane();
                mesh.AssetPath = "Primitive::Plane";
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // --- Entités spécifiques ---
        if (ImGui::MenuItem("Camera")) {
            auto entity = m_Context->CreateEntity("Camera");
            // entity.AddComponent<CameraComponent>();
        }

        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Directional Light")) {
                auto entity = m_Context->CreateEntity("Directional Light");

                // Indispensable pour que le Renderer puisse calculer la direction
                if (!entity.HasComponent<TransformComponent>()) {
                    entity.AddComponent<TransformComponent>();
                }

                // On ajoute le vrai composant
                entity.AddComponent<DirectionalLightComponent>();
            }
            if (ImGui::MenuItem("Point Light")) {
                auto entity = m_Context->CreateEntity("Point Light");
                // entity.AddComponent<LightComponent>(LightType::Point);
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    if (m_SelectionContext) {
        DrawComponents(m_SelectionContext);
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

            // On affiche le chemin du fichier source
            ImGui::TextDisabled("Source: %s", prefabRoot.GetComponent<PrefabComponent>().PrefabPath.c_str());

            // Fenêtre interne (Child) pour la mini hiérarchie
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::BeginChild("MiniHierarchy", ImVec2(0, 150), true);
            DrawMiniHierarchy(prefabRoot); // On lance le dessin !
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
    ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
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

        if (ImGui::MenuItem("Create Child Entity")) {
            Entity child = m_Context->CreateEntity("New Child");
            m_Context->ParentEntity(child, entity);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete Entity")) {
            entityDeleted = true; // Suppression différée pour éviter un crash
        }
        ImGui::EndPopup();
    }

    if (opened) {
        // --- NOUVEAU : DESSIN RÉCURSIF DES ENFANTS ---
        if (entity.HasComponent<RelationshipComponent>()) {
            entt::entity childID = entity.GetComponent<RelationshipComponent>().FirstChild;

            while (childID != entt::null) {
                Entity child{childID, m_Context.get()};
                DrawEntityNode(child); // La fonction s'appelle elle-même !

                // On passe au frère suivant
                childID = child.GetComponent<RelationshipComponent>().NextSibling;
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

    // On dessine le noeud
    bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)node, flags, "%s", tag.c_str());

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