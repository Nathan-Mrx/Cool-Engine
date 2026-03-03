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
    glm::vec3 Position = { -300.0f, 0.0f, 100.0f };
    glm::vec3 Front = { 1.0f, 0.0f, 0.0f };
    glm::vec3 WorldUp = { 0.0f, 0.0f, 1.0f };

    float Yaw = 0.0f;
    float Pitch = 0.0f;

    CameraComponent() = default;

    void OnImGuiRender() {
        ImGui::DragFloat3("Position", &Position[0], 0.1f);
        ImGui::SliderFloat("Yaw", &Yaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Pitch", &Pitch, -89.0f, 89.0f);

        ImGui::Text("Front Vector: %.2f, %.2f, %.2f", Front.x, Front.y, Front.z);
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

// --- RÉFLEXION STATIQUE (Nouveau Standard) ---

// Cette liste permet à l'Inspector d'itérer automatiquement sur tous les types
// sans avoir à modifier manuellement Application.cpp à chaque nouveau composant.
using AllComponents = std::tuple<
    TagComponent,
    TransformComponent,
    ColorComponent,
    CameraComponent,
    MeshComponent,
    DirectionalLightComponent
>;