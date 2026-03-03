#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <string>
#include <tuple>

#include "renderer/Mesh.h"

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
    Vector3 Location;
    Vector3 Rotation; // Stocké en degrés (Euler) pour l'éditeur
    Vector3 Scale = { 1.0f, 1.0f, 1.0f };

    TransformComponent() = default;

    void OnImGuiRender() {
        // Utilisation de DragFloat3 pour une édition précise et rapide sur CachyOS
        ImGui::DragFloat3("Location", Location.Data(), 0.1f);
        ImGui::DragFloat3("Rotation", Rotation.Data(), 0.1f);
        ImGui::DragFloat3("Scale", Scale.Data(), 0.1f);
    }

    // Calcule la matrice finale (Translation * Rotation * Scale)
    glm::mat4 GetTransform() const {
        glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.x), { 1, 0, 0 })
                           * glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.y), { 0, 1, 0 })
                           * glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.z), { 0, 0, 1 });

        return glm::translate(glm::mat4(1.0f), Location.ToGlm())
             * rotation
             * glm::scale(glm::mat4(1.0f), Scale.ToGlm());
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

    void OnImGuiRender() {
        if (MeshData) {
            ImGui::Text("Mesh Loaded: Yes");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "No Mesh Assigned");
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