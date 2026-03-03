#include "SceneHierarchyPanel.h"
#include "../../ecs/Components.h"
#include <imgui.h>
#include <imgui_internal.h>

// --- RÉFLEXION UI ---
template <typename T>
const char* GetComponentName() {
    if constexpr (std::is_same_v<T, TagComponent>) return "Tag";
    if constexpr (std::is_same_v<T, TransformComponent>) return "Transform";
    if constexpr (std::is_same_v<T, ColorComponent>) return "Color";
    if constexpr (std::is_same_v<T, CameraComponent>) return "Camera";
    if constexpr (std::is_same_v<T, MeshComponent>) return "Mesh";
    if constexpr (std::is_same_v<T, DirectionalLightComponent>) return "Directional Light";
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
        if (ImGui::BeginMenu("3D Object"))
        {
            if (ImGui::MenuItem("Cube")) {
                auto entity = m_Context->CreateEntity("Cube");

                // On s'assure que l'entité a une position dans le monde
                if (!entity.HasComponent<TransformComponent>()) {
                    entity.AddComponent<TransformComponent>();
                }

                // On lui donne une couleur par défaut
                entity.AddComponent<ColorComponent>(glm::vec3(0.8f, 0.8f, 0.8f));

                // On prépare le conteneur du Mesh (qui affichera le bouton "Load Mesh" dans l'Inspector)
                entity.AddComponent<MeshComponent>();
            }
            // ... (Fais la même chose pour Sphere et Plane si besoin)
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
    ImGui::End();
}

void SceneHierarchyPanel::DrawComponents(Entity entity) {
    // Utilisation de la réflexion sur le tuple de composants défini dans Components.h
    std::apply([&](auto... args) {
        (DrawComponentUI<decltype(args)>(entity, m_Context->m_Registry), ...);
    }, AllComponents{});
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

