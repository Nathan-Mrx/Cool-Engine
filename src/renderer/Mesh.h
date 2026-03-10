#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <vulkan/vulkan.h>

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
};

class Mesh {
public:
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices);
    ~Mesh();

    void Draw();

    // --- NOUVEAU : Les getters pour la preview ---
    unsigned int GetVAO() const { return m_VAO; }
    unsigned int GetIndicesCount() const { return (unsigned int)m_Indices.size(); }

    VkBuffer GetVertexBuffer() const { return m_VertexBuffer; }
    VkBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:
    void SetupMesh();
    void SetupVulkanMesh();
    void CleanupVulkanMesh();

    unsigned int m_VAO, m_VBO, m_EBO;
    std::vector<Vertex> m_Vertices;
    std::vector<unsigned int> m_Indices;

    // -- Vulkan --
    VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_VertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_IndexBufferMemory = VK_NULL_HANDLE;
};