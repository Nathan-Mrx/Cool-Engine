#pragma once

class PhysicsEngine {
public:
    static void Init();
    static void Shutdown();
    static void Update(float ts);

    // On rajoutera plus tard des fonctions comme Raycast() ou GetSystem()
};