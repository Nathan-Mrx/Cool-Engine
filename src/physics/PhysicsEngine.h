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
    // On rajoutera plus tard des fonctions comme Raycast() ou GetSystem()
};
