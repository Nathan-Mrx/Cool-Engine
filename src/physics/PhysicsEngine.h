#pragma once
#include <cstdint>
#include <glm/fwd.hpp>
#include <glm/vec3.hpp>

class PhysicsEngine {
public:
    static void Init();
    static void Shutdown();
    static void Update(float ts);

    static uint32_t CreateBoxBody(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& halfExtents, int type, float mass);
    static void DestroyBody(uint32_t bodyID);
    static void GetBodyTransform(uint32_t bodyID, glm::vec3& outPosition, glm::quat& outRotation);

    // Permet de forcer une vitesse (pratique pour le déplacement du joueur)
    static void SetLinearVelocity(uint32_t bodyID, const glm::vec3& velocity);

    // Permet d'ajouter une impulsion ou une force (pratique pour un saut)
    static void AddForce(uint32_t bodyID, const glm::vec3& force);

    // On rajoutera plus tard des fonctions comme Raycast() ou GetSystem()
};
