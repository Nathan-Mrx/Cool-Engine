#include "PrimitiveFactory.h"
#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>

// =========================================================================
// LE FIX ULTIME : CALCUL DES TANGENTES (Fini le flickering et les taches !)
// =========================================================================
static void ComputeTangents(std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    // Initialisation
    for (auto& v : vertices) v.Tangent = glm::vec3(0.0f);

    // Calcul géométrique par triangle
    for (size_t i = 0; i < indices.size(); i += 3) {
        Vertex& v0 = vertices[indices[i]];
        Vertex& v1 = vertices[indices[i+1]];
        Vertex& v2 = vertices[indices[i+2]];

        glm::vec3 e1 = v1.Position - v0.Position;
        glm::vec3 e2 = v2.Position - v0.Position;
        glm::vec2 duv1 = v1.TexCoords - v0.TexCoords;
        glm::vec2 duv2 = v2.TexCoords - v0.TexCoords;

        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        if (std::abs(det) < 0.0001f) continue; // Évite la division par zéro fatale

        float f = 1.0f / det;
        glm::vec3 t = f * (duv2.y * e1 - duv1.y * e2);

        v0.Tangent += t;
        v1.Tangent += t;
        v2.Tangent += t;
    }

    // Lissage et Orthogonalisation de Gram-Schmidt
    for (auto& v : vertices) {
        if (glm::length(v.Tangent) > 0.0001f) {
            v.Tangent = glm::normalize(v.Tangent - glm::dot(v.Tangent, v.Normal) * v.Normal);
        } else {
            // Sécurité absolue si UV cassés
            glm::vec3 up = std::abs(v.Normal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            v.Tangent = glm::normalize(glm::cross(up, v.Normal));
        }
    }
}

// =========================================================================
// --- PLANE (Converti en Z-Up) ---
// =========================================================================
std::shared_ptr<Mesh> PrimitiveFactory::CreatePlane() {
    float s = 50.0f; // 100cm (1m) au total
    std::vector<Vertex> vertices = {
        {{-s, -s, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{ s, -s, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ s,  s, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-s,  s, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}
    };
    std::vector<unsigned int> indices = { 0, 1, 2, 2, 3, 0 };
    ComputeTangents(vertices, indices);
    return std::make_shared<Mesh>(vertices, indices);
}

// =========================================================================
// --- CUBE (Converti en Z-Up) ---
// =========================================================================
std::shared_ptr<Mesh> PrimitiveFactory::CreateCube() {
    float s = 50.0f; // 100cm (1m) au total
    std::vector<Vertex> vertices = {
        // Front (-Y)
        {{-s, -s, -s}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}}, {{ s, -s, -s}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ s, -s,  s}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}}, {{-s, -s,  s}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        // Back (+Y)
        {{ s,  s, -s}, {0.0f,  1.0f, 0.0f}, {0.0f, 0.0f}}, {{-s,  s, -s}, {0.0f,  1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-s,  s,  s}, {0.0f,  1.0f, 0.0f}, {1.0f, 1.0f}}, {{ s,  s,  s}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
        // Left (-X)
        {{-s,  s, -s}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}, {{-s, -s, -s}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-s, -s,  s}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}}, {{-s,  s,  s}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        // Right (+X)
        {{ s, -s, -s}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}, {{ s,  s, -s}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ s,  s,  s}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}}, {{ s, -s,  s}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        // Top (+Z)
        {{-s, -s,  s}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}, {{ s, -s,  s}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ s,  s,  s}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}, {{-s,  s,  s}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        // Bottom (-Z)
        {{-s,  s, -s}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}}, {{ s,  s, -s}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{ s, -s, -s}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}}, {{-s, -s, -s}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}}
    };
    std::vector<unsigned int> indices = {
        0,  1,  2,  2,  3,  0,       4,  5,  6,  6,  7,  4,
        8,  9,  10, 10, 11, 8,       12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,      20, 21, 22, 22, 23, 20
    };
    ComputeTangents(vertices, indices);
    return std::make_shared<Mesh>(vertices, indices);
}

// =========================================================================
// --- SPHERE (Convertie en Z-Up) ---
// =========================================================================
std::shared_ptr<Mesh> PrimitiveFactory::CreateSphere(unsigned int xSegments, unsigned int ySegments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    float radius = 50.0f;

    for (unsigned int y = 0; y <= ySegments; ++y) {
        for (unsigned int x = 0; x <= xSegments; ++x) {
            float xSegment = (float)x / (float)xSegments;
            float ySegment = (float)y / (float)ySegments;

            // --- LE FIX Z-UP EST ICI : L'axe vertical est maintenant Z ! ---
            float xPos = std::cos(xSegment * 2.0f * glm::pi<float>()) * std::sin(ySegment * glm::pi<float>());
            float yPos = std::sin(xSegment * 2.0f * glm::pi<float>()) * std::sin(ySegment * glm::pi<float>());
            float zPos = std::cos(ySegment * glm::pi<float>());

            Vertex vertex;
            vertex.Position = glm::vec3(xPos, yPos, zPos) * radius;
            vertex.Normal = glm::vec3(xPos, yPos, zPos);
            vertex.TexCoords = glm::vec2(xSegment, ySegment);
            vertices.push_back(vertex);
        }
    }

    for (unsigned int y = 0; y < ySegments; ++y) {
        for (unsigned int x = 0; x < xSegments; ++x) {
            // Triangle 1 (Sens inverse des aiguilles d'une montre !)
            indices.push_back((y + 1) * (xSegments + 1) + x);
            indices.push_back(y * (xSegments + 1) + x + 1);
            indices.push_back(y * (xSegments + 1) + x);

            // Triangle 2 (Sens inverse des aiguilles d'une montre !)
            indices.push_back((y + 1) * (xSegments + 1) + x);
            indices.push_back((y + 1) * (xSegments + 1) + x + 1);
            indices.push_back(y * (xSegments + 1) + x + 1);
        }
    }

    // Application des tangentes calculées proprement
    ComputeTangents(vertices, indices);
    return std::make_shared<Mesh>(vertices, indices);
}