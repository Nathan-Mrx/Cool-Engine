#pragma once
#include "RendererAPI.h"
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <set>
#include <glm/glm.hpp>
#include <unordered_map>
#include <entt/entt.hpp>

#include "Mesh.h"

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// --- LA STRUCTURE DU MATÉRIAU PBR ---
struct MaterialUBO {
    glm::vec4 baseColor = glm::vec4(1.0f);
    glm::vec4 cameraPos = glm::vec4(0.0f); // <--- NOUVEAU
    float metallic = 0.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    float padding = 0.0f;
    glm::mat4 lightSpaceMatrices[4]; // Les 4 matrices solaires
    glm::vec4 cascadeSplits;         // Les distances de séparation
};

struct VulkanMaterial {
    std::vector<VkBuffer> UniformBuffers;
    std::vector<VkDeviceMemory> UniformBuffersMemory;
    std::vector<void*> UniformBuffersMapped;
    std::vector<VkDescriptorSet> DescriptorSets;
};

struct VulkanTexture {
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    void* ImGuiDescriptor = nullptr;
};

class VulkanFramebuffer;

class VulkanRenderer : public RendererAPI {
public:
    VulkanRenderer() = default;
    virtual ~VulkanRenderer() = default;

    void Init() override;
    void Shutdown() override;

    void Clear() override;
    void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) override;
    void RenderScene(Scene* scene, int renderMode) override;
    void DrawGrid(bool enable) override;
    void EndScene() override;
    void SetShadowResolution(uint32_t resolution) override;

    uint32_t GetIrradianceMapID() override { return 0; }
    uint32_t GetPrefilterMapID() override { return 0; }
    uint32_t GetBRDFLUTID() override { return 0; }

    void InitImGui(GLFWwindow* window) override;
    void BeginImGuiFrame() override;
    void EndImGuiFrame() override;
    void ShutdownImGui() override;

    void SetTargetFramebuffer(VulkanFramebuffer* fb) { m_TargetFramebuffer = fb; }
    void BeginFrameIfNeeded();

    static VulkanRenderer* Get();

    [[nodiscard]] VkDevice GetDevice() const { return m_Device; }
    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    [[nodiscard]] VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
    [[nodiscard]] VkCommandPool GetCommandPool() const { return m_CommandPool; }

    // Fonctions d'allocation mémoire et de commandes
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    [[nodiscard]] VkCommandBuffer GetCurrentCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
    [[nodiscard]] VkRenderPass GetSceneRenderPass() const { return m_SceneRenderPass; }

    void SubmitPushConstant(const glm::mat4& matrix) override;

    [[nodiscard]] VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }

    VulkanTexture* GetDefaultTexture() const { return m_DefaultTexture; }

    bool IsImGuiInitialized() const { return m_ImGuiPool != VK_NULL_HANDLE; }

    void TrackTexture(VulkanTexture* tex) { m_TrackedTextures.push_back(tex); }

    static constexpr uint32_t SHADOW_MAP_CASCADE_COUNT = 4;
    static constexpr uint32_t SHADOW_MAP_RESOLUTION = 4096;

    void PrepareShadows(Scene* scene);

    void RenderMaterialPreview(Mesh* mesh, VulkanFramebuffer* target, glm::mat4 model, glm::mat4 view, glm::mat4 proj, glm::vec3 camPos, VulkanTexture* albedo, VulkanTexture* normal, VulkanTexture* metallic, VulkanTexture* roughness, VulkanTexture* ao, glm::vec4 colorVal, float metallicVal, float roughnessVal, float aoVal);


private:
    // --- LES ÉTAPES D'INITIALISATION ---
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    bool IsDeviceSuitable(VkPhysicalDevice device);

    void CreateLogicalDevice();
    bool FindQueueFamilies(VkPhysicalDevice device, uint32_t& outGraphics, uint32_t& outPresent);

    void CreateSwapChain();
    void CreateImageViews();
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    void CreateRenderPass();
    void CreateSceneRenderPass();

    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffer();
    void CreateSyncObjects();

    VkShaderModule CreateShaderModule(const std::vector<char>& code);

    void CreateDescriptorSetLayout();

    void CreateDescriptorPool();

    VulkanMaterial CreateVulkanMaterial(VulkanTexture* albedo, VulkanTexture* normal, VulkanTexture* metallic, VulkanTexture* roughness, VulkanTexture* ao);
    void DestroyVulkanMaterial(VulkanMaterial& mat);

    VulkanTexture* CreateSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    void CreateSkyboxPipeline();

    void GenerateBRDFLUT();

    void GenerateEnvironmentCubemap();
    void GenerateIrradianceCubemap();
    void GeneratePrefilterCubemap();

    void CreateShadowResources();
    void CreateShadowPipeline();
    void CreateShadowRenderPass();

    std::array<glm::mat4, SHADOW_MAP_CASCADE_COUNT> CalculateCascadeMatrices();
    void RenderShadows(VkCommandBuffer cmdBuf, Scene* scene, const std::array<glm::mat4, SHADOW_MAP_CASCADE_COUNT>& cascadeMatrices);

    // --- VARIABLES VULKAN ---
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;

    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamilyIndex = 0;
    uint32_t m_PresentQueueFamilyIndex = 0;

    // --- VARIABLES SWAPCHAIN ---
    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_SwapChainImages;
    VkFormat m_SwapChainImageFormat;
    VkExtent2D m_SwapChainExtent;
    std::vector<VkImageView> m_SwapChainImageViews;

    VkRenderPass m_RenderPass = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_SwapChainFramebuffers;

    // --- LA RÈGLE D'OR DE LA PERFORMANCE ---
    const int MAX_FRAMES_IN_FLIGHT = 2;

    // --- COMMANDES ---
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_CommandBuffers; // <-- Devient un tableau (2 stylos)

    // --- SYNCHRONISATION ---
    std::vector<VkSemaphore> m_ImageAvailableSemaphores; // 2 feux
    std::vector<VkSemaphore> m_RenderFinishedSemaphores; // 2 feux
    std::vector<VkFence> m_InFlightFences;               // 2 feux

    // --- INDEX ---
    uint32_t m_CurrentImageIndex = 0; // L'image physique de l'écran (0 à 3)
    uint32_t m_CurrentFrame = 0;      // La frame logique en cours de calcul (0 ou 1)

    VkDescriptorPool m_ImGuiPool = VK_NULL_HANDLE;

    // --- LE PIPELINE ---
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE; // Pour envoyer des variables globales plus tard
    VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;     // L'état complet de la carte graphique

    bool m_IsFrameStarted = false;
    VulkanFramebuffer* m_TargetFramebuffer = nullptr;

    VkRenderPass m_SceneRenderPass = VK_NULL_HANDLE;

    glm::mat4 m_SceneViewMatrix = glm::mat4(1.0f);
    glm::mat4 m_SceneProjectionMatrix = glm::mat4(1.0f);
    glm::vec3 m_CameraPos = glm::vec3(0.0f);

    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;

    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

    std::unordered_map<entt::entity, VulkanMaterial> m_EntityMaterials;

    VulkanTexture* m_DefaultTexture = nullptr;
    VulkanTexture* m_DefaultWhiteTexture = nullptr;
    VulkanTexture* m_DefaultBlackTexture = nullptr;
    VulkanTexture* m_DefaultNormalTexture = nullptr;

    VkDescriptorSetLayout m_SkyboxDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_SkyboxPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_SkyboxPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_SkyboxDescriptorSet = VK_NULL_HANDLE;

    std::shared_ptr<Mesh> m_SkyboxCube;
    VulkanTexture* m_SkyboxTexture = nullptr;

    VulkanTexture* m_BrdfLutTexture = nullptr;

    std::vector<VulkanTexture*> m_TrackedTextures;

    VulkanTexture* m_EnvironmentCubemap = nullptr;
    VulkanTexture* m_IrradianceCubemap = nullptr;
    VulkanTexture* m_PrefilterCubemap = nullptr;

    VkImage m_ShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_ShadowImageMemory = VK_NULL_HANDLE;
    VkImageView m_ShadowImageView = VK_NULL_HANDLE;             // Vue globale (Array) pour le Shader PBR
    std::vector<VkImageView> m_ShadowCascadeViews;              // Vues individuelles pour écrire dedans
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;

    VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_ShadowFramebuffers;            // Un Framebuffer par cascade

    VkPipelineLayout m_ShadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ShadowPipeline = VK_NULL_HANDLE;

    std::array<glm::mat4, SHADOW_MAP_CASCADE_COUNT> m_CurrentShadowMatrices;
    glm::vec4 m_CurrentCascadeSplits;

    // --- VALIDATION LAYERS ---
    const std::vector<const char*> m_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

#ifdef NDEBUG
    const bool m_EnableValidationLayers = false;
#else
    const bool m_EnableValidationLayers = true;
#endif

    bool CheckValidationLayerSupport();
};