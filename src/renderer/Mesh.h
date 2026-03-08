#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
};

class Mesh {
public:
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices);
    void Draw();

    // --- NOUVEAU : Les getters pour la preview ---
    unsigned int GetVAO() const { return m_VAO; }
    unsigned int GetIndicesCount() const { return (unsigned int)m_Indices.size(); }

private:
    void SetupMesh();

    unsigned int m_VAO, m_VBO, m_EBO;
    std::vector<Vertex> m_Vertices;
    std::vector<unsigned int> m_Indices;
};