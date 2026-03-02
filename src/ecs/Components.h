#pragma once
#include <glm/glm.hpp>

struct TransformComponent {
    glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };
    // On garde ça simple pour commencer !
};

struct ColorComponent {
    glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
};

struct CameraComponent {
    glm::vec3 Position = { -3.0f, 0.0f, 0.0f };
    glm::vec3 Front = { 1.0f, 0.0f, 0.0f };
    glm::vec3 WorldUp = { 0.0f, 0.0f, 1.0f };

    // On initialise le Yaw à 0 (regarde vers X+) et le Pitch à 0 (horizontal)
    float Yaw = 0.0f;
    float Pitch = 0.0f;
};

struct TagComponent {
    std::string Tag;

    TagComponent() = default;
    TagComponent(const std::string& tag) : Tag(tag) {}
};