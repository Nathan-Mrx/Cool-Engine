#include "Mesh.h"
#include <glad/glad.h>
#include "RendererAPI.h" // Indispensable pour la vérification !

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices)
    : m_Vertices(vertices), m_Indices(indices)
{
    SetupMesh();
}

void Mesh::SetupMesh() {
    // --- SÉCURITÉ VULKAN : On n'alloue pas de VBO/VAO OpenGL ---
    if (RendererAPI::GetAPI() != RendererAPI::API::OpenGL) return;

    // Génération des IDs
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    // 1. Chargement des sommets (VBO)
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, m_Vertices.size() * sizeof(Vertex), &m_Vertices[0], GL_STATIC_DRAW);

    // 2. Chargement des indices (EBO)
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Indices.size() * sizeof(unsigned int), &m_Indices[0], GL_STATIC_DRAW);

    // 3. Définition des attributs de sommet
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));

    glBindVertexArray(0);
}

void Mesh::Draw() {
    // --- SÉCURITÉ VULKAN : Pas de dessin OpenGL pur ---
    if (RendererAPI::GetAPI() != RendererAPI::API::OpenGL) return;

    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(m_Indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}