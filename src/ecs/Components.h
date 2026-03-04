#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <string>
#include <tuple>
#include <filesystem>

#include "renderer/Mesh.h"
#include <nfd.hpp>
#include "../renderer/ModelLoader.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "core/UUID.h"
#include "scene/ScriptableEntity.h"

// --- STRUCTURES DE DONNÉES DE BASE ---

struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vector3() = default;
    Vector3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    glm::vec3 ToGlm() const { return { x, y, z }; }
    float* Data() { return &x; }
};

// --- COMPOSANTS ECS ---

struct TagComponent {
    std::string Tag;

    TagComponent() = default;
    TagComponent(const std::string& tag) : Tag(tag) {}

    void OnImGuiRender() {
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, Tag.c_str(), sizeof(buffer));
        if (ImGui::InputText("Entity Name", buffer, sizeof(buffer))) {
            Tag = std::string(buffer);
        }
    }
};

struct TransformComponent {
    glm::vec3 Location = { 0.0f, 0.0f, 0.0f };
    glm::vec3 RotationEuler = { 0.0f, 0.0f, 0.0f }; // <-- LE CACHE POUR L'UI
    glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // <-- POUR LES MATHS ET LA PHYSIQUE
    glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

    TransformComponent() = default;
    TransformComponent(const TransformComponent&) = default;
    TransformComponent(const glm::vec3& translation) : Location(translation) {}

    glm::mat4 GetTransform() const {
        return glm::translate(glm::mat4(1.0f), Location)
             * glm::toMat4(Rotation)
             * glm::scale(glm::mat4(1.0f), Scale);
    }

    void OnImGuiRender() {
        ImGui::DragFloat3("Location", glm::value_ptr(Location), 0.1f);

        // On modifie directement le cache Euler (plus de sauts étranges en tapant les chiffres !)
        if (ImGui::DragFloat3("Rotation", glm::value_ptr(RotationEuler), 0.1f)) {
            // Si l'utilisateur change l'UI, on force le Quaternion à se mettre à jour
            Rotation = glm::quat(glm::radians(RotationEuler));
        }

        ImGui::DragFloat3("Scale", glm::value_ptr(Scale), 0.1f);
    }

    glm::vec3 GetForwardVector() const {
        // Comme tu es en Left-Handed (perspectiveLH), "l'avant" est généralement +Z.
        // On prend un vecteur qui pointe vers l'avant, et on lui applique notre Quaternion !
        return glm::rotate(Rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    glm::vec3 GetUpVector() const {
        return glm::rotate(Rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    glm::vec3 GetRightVector() const {
        return glm::rotate(Rotation, glm::vec3(1.0f, 0.0f, 0.0f));
    }
};

struct ColorComponent {
    glm::vec3 Color = { 1.0f, 1.0f, 1.0f };

    ColorComponent() = default;
    ColorComponent(const glm::vec3& color) : Color(color) {}

    void OnImGuiRender() {
        ImGui::ColorEdit3("Albedo Color", &Color[0]);
    }
};

struct CameraComponent {
    bool Primary = true; // Si vrai, c'est cette caméra qui est rendue à l'écran
    float FOV = 90.0f;   // Champ de vision (en degrés)
    float NearClip = 0.1f;
    float FarClip = 100000.0f;

    CameraComponent() = default;
    CameraComponent(const CameraComponent&) = default;

    void OnImGuiRender() {
        ImGui::Checkbox("Primary Camera", &Primary);
        ImGui::DragFloat("FOV", &FOV, 0.5f, 10.0f, 150.0f);
        ImGui::DragFloat("Near Clip", &NearClip, 0.1f, 0.01f, 100.0f);
        ImGui::DragFloat("Far Clip", &FarClip, 100.0f, 100.0f, 1000000.0f);
    }
};

struct MeshComponent {
    std::shared_ptr<Mesh> MeshData;
    std::string AssetPath;

    void OnImGuiRender() {
        if (MeshData) {
            ImGui::TextWrapped("Path: %s", AssetPath.c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "No Mesh Assigned");
        }

        ImGui::Spacing();

        // On crée une zone visuelle pour le Drag & Drop
        ImGui::Button("Drop .obj Here to Load", ImVec2(-1, 40));

        // --- NOUVEAU : DRAG & DROP TARGET (Inspector) ---
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* path = (const char*)payload->Data;
                std::filesystem::path filepath = path;

                if (filepath.extension() == ".obj" || filepath.extension() == ".fbx") {
                    AssetPath = filepath.string();
                    MeshData = ModelLoader::LoadModel(AssetPath);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Garde l'ancien bouton NFD pour la rétrocompatibilité si tu veux chercher en dehors du projet
        if (ImGui::Button("Or Browse...", ImVec2(-1, 0))) {
            nfdchar_t* outPath = nullptr;
            if (NFD::OpenDialog(outPath, nullptr, 0, nullptr) == NFD_OKAY) {
                AssetPath = outPath;
                MeshData = ModelLoader::LoadModel(AssetPath);
                NFD::FreePath(outPath);
            }
        }
    }
};

struct DirectionalLightComponent {
    glm::vec3 Color = { 1.0f, 1.0f, 1.0f }; // Lumière blanche par défaut
    float AmbientIntensity = 0.2f;          // Lumière ambiante (pour ne pas avoir de noir total)
    float DiffuseIntensity = 0.8f;          // Puissance de la lumière directe

    void OnImGuiRender() {
        ImGui::ColorEdit3("Light Color", &Color[0]);
        ImGui::DragFloat("Ambient", &AmbientIntensity, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Diffuse", &DiffuseIntensity, 0.01f, 0.0f, 1.0f);
    }
};

// Définit le comportement de l'objet dans le monde physique
enum class RigidBodyType { Static = 0, Kinematic, Dynamic };

struct RigidBodyComponent {
    RigidBodyType Type = RigidBodyType::Static;
    float Mass = 1.0f;

    // Identifiant interne utilisé par Jolt (on l'initialise à une valeur invalide)
    uint32_t RuntimeBodyID = 0xFFFFFFFF;

    RigidBodyComponent() = default;
    RigidBodyComponent(const RigidBodyComponent&) = default;

    void OnImGuiRender() {
        const char* bodyTypeStrings[] = { "Static", "Kinematic", "Dynamic" };
        const char* currentBodyTypeString = bodyTypeStrings[(int)Type];

        if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
            for (int i = 0; i < 3; i++) {
                bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                    Type = (RigidBodyType)i;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (Type == RigidBodyType::Dynamic) {
            ImGui::DragFloat("Mass", &Mass, 0.1f, 0.01f, 10000.0f);
        }
    }
};

// Définit la forme de la boîte de collision
struct BoxColliderComponent {
    glm::vec3 HalfSize = { 0.5f, 0.5f, 0.5f }; // La taille de la boîte (depuis le centre)
    glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };   // Décalage par rapport au Transform de l'entité

    float Friction = 0.5f;
    float Restitution = 0.0f; // Bounciness (Rebond)

    BoxColliderComponent() = default;
    BoxColliderComponent(const BoxColliderComponent&) = default;

    void OnImGuiRender() {
        // Multiplié par 2 visuellement pour que l'utilisateur rentre la taille totale (plus intuitif)
        glm::vec3 size = HalfSize * 2.0f;
        if (ImGui::DragFloat3("Size", glm::value_ptr(size), 0.1f)) {
            HalfSize = size / 2.0f;
        }

        ImGui::DragFloat3("Offset", glm::value_ptr(Offset), 0.1f);
        ImGui::DragFloat("Friction", &Friction, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Restitution", &Restitution, 0.01f, 0.0f, 1.0f);
    }
};

struct NativeScriptComponent {
    std::string ScriptName = "None";
    ScriptableEntity* Instance = nullptr;

    // Pointeurs de fonctions pour allouer et désallouer la mémoire dynamiquement
    ScriptableEntity* (*InstantiateScript)() = nullptr;
    void (*DestroyScript)(NativeScriptComponent*) = nullptr;

    // Fonction template magique pour lier un script au composant
    template<typename T>
    void Bind() {
        InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
        DestroyScript = [](NativeScriptComponent* nsc) {
            delete nsc->Instance;
            nsc->Instance = nullptr;
        };
    }

    void OnImGuiRender() {}
};

struct RelationshipComponent {
    entt::entity Parent = entt::null;
    entt::entity FirstChild = entt::null;
    entt::entity PreviousSibling = entt::null;
    entt::entity NextSibling = entt::null;

    RelationshipComponent() = default;
    RelationshipComponent(const RelationshipComponent&) = default;

    void OnImGuiRender() {
        // Pour l'instant, on n'affiche rien dans l'Inspector.
        // La hiérarchie se gérera visuellement dans l'arbre !
    }
};

struct IDComponent {
    UUID ID;

    IDComponent() = default;
    IDComponent(const IDComponent&) = default;
    IDComponent(UUID id) : ID(id) {}

    void OnImGuiRender() {} // Non modifiable par l'utilisateur
};

// --- RÉFLEXION STATIQUE (Nouveau Standard) ---

// Cette liste permet à l'Inspector d'itérer automatiquement sur tous les types
// sans avoir à modifier manuellement Application.cpp à chaque nouveau composant.
using AllComponents = std::tuple<
    TagComponent,
    TransformComponent,
    ColorComponent,
    CameraComponent,
    MeshComponent,
    DirectionalLightComponent,
    RigidBodyComponent,
    BoxColliderComponent,
    NativeScriptComponent,
    RelationshipComponent,
    IDComponent
>;