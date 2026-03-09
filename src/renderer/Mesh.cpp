#include "Mesh.h"

#include <cstring>
#include <glad/glad.h>
#include "RendererAPI.h"
#include "VulkanRenderer.h" // Indispensable pour parler au GPU
#include <stdexcept>

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices)
    : m_Vertices(vertices), m_Indices(indices)
{
    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        SetupMesh();
    } else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        SetupVulkanMesh();
    }
}

Mesh::~Mesh() {
    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        CleanupVulkanMesh();
    }
    // (Note : En OpenGL, il faudrait idéalement un glDeleteBuffers ici aussi !)
}

void Mesh::SetupMesh() {
    // Ton code OpenGL d'origine reste ici, intact !
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, m_Vertices.size() * sizeof(Vertex), &m_Vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Indices.size() * sizeof(unsigned int), &m_Indices[0], GL_STATIC_DRAW);

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

// ====================================================================
// --- LA VRAIE 3D VULKAN ---
// ====================================================================
void Mesh::SetupVulkanMesh() {
    if (m_Vertices.empty() || m_Indices.empty()) return;

    VulkanRenderer* vkRenderer = VulkanRenderer::Get();
    VkDevice device = vkRenderer->GetDevice();

    // 1. === VERTEX BUFFER ===
    VkDeviceSize vertexBufferSize = sizeof(m_Vertices[0]) * m_Vertices.size();

    // Le Sas (Staging)
    VkBuffer stagingVertexBuffer;
    VkDeviceMemory stagingVertexMemory;
    vkRenderer->CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             stagingVertexBuffer, stagingVertexMemory);

    // On copie les points du CPU vers le Sas
    void* vertexData;
    vkMapMemory(device, stagingVertexMemory, 0, vertexBufferSize, 0, &vertexData);
    memcpy(vertexData, m_Vertices.data(), (size_t)vertexBufferSize);
    vkUnmapMemory(device, stagingVertexMemory);

    // Le Vrai Buffer Vidéo (Cible)
    vkRenderer->CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             m_VertexBuffer, m_VertexBufferMemory);

    // On ordonne au GPU de transférer du Sas vers la VRAM
    VkCommandBuffer vertexCmd = vkRenderer->BeginSingleTimeCommands();
    VkBufferCopy vertexCopyRegion{};
    vertexCopyRegion.size = vertexBufferSize;
    vkCmdCopyBuffer(vertexCmd, stagingVertexBuffer, m_VertexBuffer, 1, &vertexCopyRegion);
    vkRenderer->EndSingleTimeCommands(vertexCmd);

    // On détruit le Sas
    vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
    vkFreeMemory(device, stagingVertexMemory, nullptr);


    // 2. === INDEX BUFFER ===
    VkDeviceSize indexBufferSize = sizeof(m_Indices[0]) * m_Indices.size();

    VkBuffer stagingIndexBuffer;
    VkDeviceMemory stagingIndexMemory;
    vkRenderer->CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             stagingIndexBuffer, stagingIndexMemory);

    void* indexData;
    vkMapMemory(device, stagingIndexMemory, 0, indexBufferSize, 0, &indexData);
    memcpy(indexData, m_Indices.data(), (size_t)indexBufferSize);
    vkUnmapMemory(device, stagingIndexMemory);

    vkRenderer->CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             m_IndexBuffer, m_IndexBufferMemory);

    VkCommandBuffer indexCmd = vkRenderer->BeginSingleTimeCommands();
    VkBufferCopy indexCopyRegion{};
    indexCopyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(indexCmd, stagingIndexBuffer, m_IndexBuffer, 1, &indexCopyRegion);
    vkRenderer->EndSingleTimeCommands(indexCmd);

    vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
    vkFreeMemory(device, stagingIndexMemory, nullptr);
}

void Mesh::CleanupVulkanMesh() {
    VkDevice device = VulkanRenderer::Get()->GetDevice();
    if (device) {
        if (m_IndexBuffer) vkDestroyBuffer(device, m_IndexBuffer, nullptr);
        if (m_IndexBufferMemory) vkFreeMemory(device, m_IndexBufferMemory, nullptr);
        if (m_VertexBuffer) vkDestroyBuffer(device, m_VertexBuffer, nullptr);
        if (m_VertexBufferMemory) vkFreeMemory(device, m_VertexBufferMemory, nullptr);
    }
}

void Mesh::Draw() {
    if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
        glBindVertexArray(m_VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(m_Indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    } else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        // En Vulkan, le dessin se fait en injectant directement les buffers
        // dans le CommandBuffer actif de la Frame !
        VulkanRenderer* vkRenderer = VulkanRenderer::Get();
        VkCommandBuffer currentCmd = vkRenderer->GetCurrentCommandBuffer();

        if (currentCmd != VK_NULL_HANDLE && m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE) {
            VkBuffer vertexBuffers[] = {m_VertexBuffer};
            VkDeviceSize offsets[] = {0};

            // 1. On lie les sommets
            vkCmdBindVertexBuffers(currentCmd, 0, 1, vertexBuffers, offsets);
            // 2. On lie l'ordre (les indices)
            vkCmdBindIndexBuffer(currentCmd, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            // 3. On dessine !
            vkCmdDrawIndexed(currentCmd, static_cast<uint32_t>(m_Indices.size()), 1, 0, 0, 0);
        }
    }
}