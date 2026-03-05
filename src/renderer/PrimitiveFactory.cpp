#include "PrimitiveFactory.h"
#include <glm/gtc/constants.hpp>

// --- PLANE (1m x 1m) ---
std::shared_ptr<Mesh> PrimitiveFactory::CreatePlane() {
    float s = 50.0f; // Demi-taille (50cm) pour faire 1m au total
    std::vector<Vertex> vertices = {
        {{-s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}}
    };
    std::vector<unsigned int> indices = { 0, 2, 1, 2, 0, 3 };
    return std::make_shared<Mesh>(vertices, indices); // Le constructeur appelle SetupMesh() nativement
}

// --- CUBE (1m x 1m x 1m) ---
// --- CUBE (1m x 1m x 1m) ---
std::shared_ptr<Mesh> PrimitiveFactory::CreateCube() {
    float s = 50.0f;
    std::vector<Vertex> vertices = {
        // Front (Y inversé)
        {{-s, -s,  s}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}, {{ s, -s,  s}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ s,  s,  s}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}}, {{-s,  s,  s}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        // Back (Y inversé)
        {{ s, -s, -s}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}}, {{-s, -s, -s}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-s,  s, -s}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}}, {{ s,  s, -s}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        // Left (Y inversé)
        {{-s, -s, -s}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}, {{-s, -s,  s}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-s,  s,  s}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}}, {{-s,  s, -s}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        // Right (Y inversé)
        {{ s, -s,  s}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}, {{ s, -s, -s}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ s,  s, -s}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}}, {{ s,  s,  s}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        // Top (Y inversé)
        {{-s,  s,  s}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, {{ s,  s,  s}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ s,  s, -s}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}, {{-s,  s, -s}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        // Bottom (Y inversé)
        {{-s, -s, -s}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}}, {{ s, -s, -s}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ s, -s,  s}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}}, {{-s, -s,  s}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}}
    };
    std::vector<unsigned int> indices = {
        0,  1,  2,  2,  3,  0,       4,  5,  6,  6,  7,  4,
        8,  9,  10, 10, 11, 8,       12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,      20, 21, 22, 22, 23, 20
    };
    return std::make_shared<Mesh>(vertices, indices);
}

// --- SPHERE (Mathématique de génération paramétrique) ---
std::shared_ptr<Mesh> PrimitiveFactory::CreateSphere(unsigned int xSegments, unsigned int ySegments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    float radius = 50.0f;

    for (unsigned int y = 0; y <= ySegments; ++y) {
        for (unsigned int x = 0; x <= xSegments; ++x) {
            float xSegment = (float)x / (float)xSegments;
            float ySegment = (float)y / (float)ySegments;
            float xPos = std::cos(xSegment * 2.0f * glm::pi<float>()) * std::sin(ySegment * glm::pi<float>());
            float yPos = std::cos(ySegment * glm::pi<float>());
            float zPos = std::sin(xSegment * 2.0f * glm::pi<float>()) * std::sin(ySegment * glm::pi<float>());

            Vertex vertex;
            vertex.Position = glm::vec3(xPos, yPos, zPos) * radius;
            vertex.Normal = glm::vec3(xPos, yPos, zPos); // Pour une sphère à l'origine, la normale = la position normalisée
            vertex.TexCoords = glm::vec2(xSegment, ySegment);
            vertices.push_back(vertex);
        }
    }

    bool oddRow = false;
    for (unsigned int y = 0; y < ySegments; ++y) {
        for (unsigned int x = 0; x < xSegments; ++x) {
            indices.push_back((y + 1) * (xSegments + 1) + x);
            indices.push_back(y * (xSegments + 1) + x);
            indices.push_back(y * (xSegments + 1) + x + 1);

            indices.push_back((y + 1) * (xSegments + 1) + x);
            indices.push_back(y * (xSegments + 1) + x + 1);
            indices.push_back((y + 1) * (xSegments + 1) + x + 1);
        }
    }
    return std::make_shared<Mesh>(vertices, indices);
}