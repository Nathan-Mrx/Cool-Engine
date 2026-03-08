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
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffer();

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

    // --- COMMANDES ---
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

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