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
        if (!HasComponent<RigidBodyComponent>()) return;
        auto& rb = GetComponent<RigidBodyComponent>();

        // 1. On récupère la vitesse actuelle imposée par la physique (la gravité !)
        glm::vec3 currentVel = PhysicsEngine::GetLinearVelocity(rb.RuntimeBodyID);

        // 2. On prépare notre nouvelle vitesse (on garde le Z actuel pour pouvoir tomber)
        glm::vec3 targetVel(0.0f, 0.0f, currentVel.z);
        float speed = 1000.0f; // 1 mètre par seconde

        // Déplacements sur X et Y
        if (Input::IsKeyPressed(GLFW_KEY_UP) || Input::IsKeyPressed(GLFW_KEY_W))    targetVel.y += speed;
        if (Input::IsKeyPressed(GLFW_KEY_DOWN) || Input::IsKeyPressed(GLFW_KEY_S))  targetVel.y -= speed;
        if (Input::IsKeyPressed(GLFW_KEY_RIGHT) || Input::IsKeyPressed(GLFW_KEY_D)) targetVel.x += speed;
        if (Input::IsKeyPressed(GLFW_KEY_LEFT) || Input::IsKeyPressed(GLFW_KEY_A))  targetVel.x -= speed;

        // 3. On applique le mouvement
        PhysicsEngine::SetLinearVelocity(rb.RuntimeBodyID, targetVel);

        // 4. LE SAUT (Impulsion vers le haut sur l'axe Z)
        // Note : En l'état, on peut sauter à l'infini dans les airs (Flappy Bird style).
        // Il faudra un système de détection du sol (Raycast) plus tard !
        if (Input::IsKeyPressed(GLFW_KEY_SPACE)) {
            // On applique une force massive vers le haut (Z positif)
            PhysicsEngine::AddForce(rb.RuntimeBodyID, glm::vec3(0.0f, 0.0f, 500000.0f));
        }
    }

    void OnDestroy() override {
        std::cout << "[PlayerController] Entity destroyed/unpossessed." << std::endl;
    }
};

REGISTER_SCRIPT(PlayerController)