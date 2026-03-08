#pragma once
#include "RendererAPI.h"
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <set>

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

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
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffer();
    void CreateSyncObjects();

    VkShaderModule CreateShaderModule(const std::vector<char>& code);

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