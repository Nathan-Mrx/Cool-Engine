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

class Mesh;

struct AccelerationStructure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// --- LA STRUCTURE DU MATÉRIAU PBR ---
struct MaterialUBO {
    glm::vec4 baseColor = glm::vec4(1.0f);
    glm::vec4 cameraPos = glm::vec4(0.0f);
    float metallic = 0.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    float padding = 0.0f;
    glm::mat4 lightSpaceMatrices[4];
    glm::vec4 cascadeSplits;
    glm::vec4 ddgiStartPosition; // xyz = Position, w = Spacing
    glm::ivec4 ddgiProbeCount;   // xyz = Count, w = padding
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

    void Clear(Scene* scene) override;
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
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1, uint32_t layerCount = 1);
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

    void InvalidateEntityMaterial(entt::entity entityID);
    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer);


    const std::vector<const char*> m_DeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };

    AccelerationStructure CreateBLAS(VkBuffer vertexBuffer, uint32_t vertexCount, VkBuffer indexBuffer, uint32_t indexCount);
    void DestroyAccelerationStructure(AccelerationStructure& as);

    void BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances);

    void UpdateTLAS(Scene* scene);
    void PreRender(Scene* scene);

    void BeginFrame();
    void EndFrame();

    // --- DDGI & COMPUTE ---
    VulkanTexture* CreateStorageTexture(uint32_t width, uint32_t height, VkFormat format);
    void DestroyTexture(VulkanTexture* tex);

    VulkanTexture* GetDefaultWhiteTexture() const { return m_DefaultWhiteTexture; }
    VulkanTexture* GetDefaultBlackTexture() const { return m_DefaultBlackTexture; }
    VulkanTexture* GetDefaultNormalTexture() const { return m_DefaultNormalTexture; }

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

    void CreateDDGIPipeline();
    void ComputeDDGI(class DDGIVolume* volume);

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

    // --- PREVIEW MATERIAL (CACHE) ---
    VulkanMaterial m_PreviewMaterial;
    bool m_PreviewMaterialInitialized = false;

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

    struct DeletionQueue {
        std::deque<std::function<void()>> deletors;

        void push_function(std::function<void()>&& function) {
            deletors.push_back(function);
        }

        void flush() {
            // Reverse iterate pour détruire les objets dans le bon ordre (LIFO)
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
                (*it)();
            }
            deletors.clear();
        }
    };

    // --- POINTEURS DE FONCTIONS RAY TRACING (KHR) ---
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;

    DeletionQueue m_MainDeletionQueue;

    // --- RAY TRACING : TLAS (APPROCHE AAA) ---
    std::vector<AccelerationStructure> m_TLAS;
    std::vector<VkBuffer> m_TLASInstancesBuffer;
    std::vector<VkDeviceMemory> m_TLASInstancesMemory;

    std::vector<VkBuffer> m_TLASScratchBuffer;
    std::vector<VkDeviceMemory> m_TLASScratchMemory;

    // --- DDGI COMPUTE PIPELINE ---
    VkDescriptorSetLayout m_DDGIDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_DDGIPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_DDGIPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_DDGIDescriptorSet = VK_NULL_HANDLE;

#ifdef NDEBUG
    const bool m_EnableValidationLayers = false;
#else
    const bool m_EnableValidationLayers = true;
#endif

    bool CheckValidationLayerSupport();
};