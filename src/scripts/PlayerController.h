#pragma once
#include "../scene/ScriptableEntity.h"
#include "../ecs/Components.h"
#include "../core/Input.h"
#include <GLFW/glfw3.h> // Pour avoir les touches GLFW_KEY_...
#include <iostream>

#include "ScriptRegistry.h"

class PlayerController : public ScriptableEntity {
public:
    void OnCreate() override {
        std::cout << "[PlayerController] Possessed entity: " << GetComponent<TagComponent>().Tag << "!" << std::endl;
    }

    void OnUpdate(float ts) override {
        // On récupère le Transform et le RigidBody de l'entité
        auto& transform = GetComponent<TransformComponent>();
        
        // Vitesse de déplacement (en cm par seconde)
        float speed = 800.0f * ts;

        // Lecture des inputs et modification de la position
        // (Note: Plus tard, au lieu de bouger le Transform, on appliquera des forces sur le RigidBody !)
        if (Input::IsKeyPressed(GLFW_KEY_UP) || Input::IsKeyPressed(GLFW_KEY_W)) {
            transform.Location.y += speed;
        }
        if (Input::IsKeyPressed(GLFW_KEY_DOWN) || Input::IsKeyPressed(GLFW_KEY_S)) {
            transform.Location.y -= speed;
        }
        if (Input::IsKeyPressed(GLFW_KEY_RIGHT) || Input::IsKeyPressed(GLFW_KEY_D)) {
            transform.Location.x += speed;
        }
        if (Input::IsKeyPressed(GLFW_KEY_LEFT) || Input::IsKeyPressed(GLFW_KEY_A)) {
            transform.Location.x -= speed;
        }
    }

    void OnDestroy() override {
        std::cout << "[PlayerController] Entity destroyed/unpossessed." << std::endl;
    }
};

REGISTER_SCRIPT(PlayerController)