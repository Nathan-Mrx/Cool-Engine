#pragma once
#include <memory>
#include <vector>
#include "Mesh.h"

class PrimitiveFactory {
public:
    // On génère des primitives de 100 unités (1 mètre dans ton système)
    static std::shared_ptr<Mesh> CreatePlane();
    static std::shared_ptr<Mesh> CreateCube();
    static std::shared_ptr<Mesh> CreateSphere(unsigned int xSegments = 32, unsigned int ySegments = 32);
};