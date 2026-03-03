#include "SceneHierarchyPanel.h"
#include "../../ecs/Components.h"
#include <imgui.h>
#include <imgui_internal.h>

#include "renderer/PrimitiveFactory.h"

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
        DrawEntityNode(entity);
    }

    // Le clic droit dans le vide de la hiérarchie
    if (ImGui::BeginPopupContextWindow("SceneHierarchyContextWindow", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

        if (ImGui::MenuItem("Create Empty Entity")) {
            m_Context->CreateEntity("Empty Entity");
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
    ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    // On utilise l'adresse mémoire du Tag, qui est strictement unique pour chaque entité
    void* uniqueID = (void*)&entity.GetComponent<TagComponent>();
    bool opened = ImGui::TreeNodeEx(uniqueID, flags, "%s", tag.c_str());

    // 1. LE CLIC DROIT DOIT ÊTRE ICI ! (Avant le TreePop)
    bool entityDeleted = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete Entity")) {
            entityDeleted = true;
        }
        ImGui::EndPopup();
    }

    // 2. Gestion de la sélection
    if (ImGui::IsItemClicked()) {
        m_SelectionContext = entity;
    }

    // 3. Fermeture du noeud
    if (opened) {
        ImGui::TreePop();
    }

    // 4. Destruction sécurisée
    if (entityDeleted) {
        m_Context->DestroyEntity(entity);
        if (m_SelectionContext == entity) m_SelectionContext = {};
    }
}

void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene>& context) {
    m_Context = context;
    m_SelectionContext = {}; // On réinitialise la sélection lors du changement de scène
}

