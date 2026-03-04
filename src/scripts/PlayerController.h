#pragma once
#include "../scene/ScriptableEntity.h"
#include "../ecs/Components.h"
#include "../core/Input.h"
#include "../physics/PhysicsEngine.h"
#include <GLFW/glfw3.h>
#include <iostream>

#include "ScriptRegistry.h"

class PlayerController : public ScriptableEntity {
public:
    void OnCreate() override {
        std::cout << "[PlayerController] Possessed entity: " << GetComponent<TagComponent>().Tag << "!" << std::endl;
    }

    void OnUpdate(float ts) override {
        // Sécurité : On s'assure que l'entité possède bien un corps physique
        if (!HasComponent<RigidBodyComponent>()) return;

        auto& rb = GetComponent<RigidBodyComponent>();

        // On détermine la vitesse cible (ex: 5 mètres par seconde)
        glm::vec3 velocity(0.0f);
        float speed = 5.0f;

        if (Input::IsKeyPressed(GLFW_KEY_UP) || Input::IsKeyPressed(GLFW_KEY_W)) {
            velocity.y += speed; // (Ou Z selon ton axe Forward)
        }
        if (Input::IsKeyPressed(GLFW_KEY_DOWN) || Input::IsKeyPressed(GLFW_KEY_S)) {
            velocity.y -= speed;
        }
        if (Input::IsKeyPressed(GLFW_KEY_RIGHT) || Input::IsKeyPressed(GLFW_KEY_D)) {
            velocity.x += speed;
        }
        if (Input::IsKeyPressed(GLFW_KEY_LEFT) || Input::IsKeyPressed(GLFW_KEY_A)) {
            velocity.x -= speed;
        }

        // On envoie la commande à Jolt !
        // Note: Pour conserver la gravité, il faudrait idéalement lire la vélocité Z/Y actuelle de Jolt et la conserver.
        // Mais pour l'instant, c'est parfait pour un test !
        if (velocity.x != 0.0f || velocity.y != 0.0f) {
            PhysicsEngine::SetLinearVelocity(rb.RuntimeBodyID, velocity);
        }
    }

    void OnDestroy() override {
        std::cout << "[PlayerController] Entity destroyed/unpossessed." << std::endl;
    }
};

REGISTER_SCRIPT(PlayerController)