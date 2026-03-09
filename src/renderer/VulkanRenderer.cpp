#include "VulkanRenderer.h"
#include "core/Application.h"

#include <stdexcept>
#include <cstring>
#include <fstream>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "Mesh.h"
#include "PrimitiveFactory.h"
#include "TextureLoader.h"
#include "VulkanFramebuffer.h"
#include "ecs/Components.h"

static std::vector<char> ReadFile(const std::string& filename) {
    // On lit à la fin (ate) et en binaire (binary) pour avoir la taille exacte direct
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Erreur fatale: Impossible d'ouvrir le shader " + filename);

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

static VulkanRenderer* s_VkInstance = nullptr;

VulkanRenderer* VulkanRenderer::Get() {
    return s_VkInstance;
}

void VulkanRenderer::Init() {
    s_VkInstance = this;

    std::cout << "\n===========================================\n";
    std::cout << "[VulkanRenderer] Initialisation en cours...\n";
    std::cout << "===========================================\n\n";

    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateSceneRenderPass();
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateDescriptorPool();
    CreateCommandPool();

    m_DefaultWhiteTexture = CreateSolidColorTexture(255, 255, 255, 255);
    m_DefaultBlackTexture = CreateSolidColorTexture(0, 0, 0, 255);
    m_DefaultNormalTexture = CreateSolidColorTexture(128, 128, 255, 255);

    m_SkyboxCube = PrimitiveFactory::CreateCube();
    m_SkyboxTexture = static_cast<VulkanTexture*>(TextureLoader::LoadHDR("assets/textures/sky.hdr"));
    CreateSkyboxPipeline();

    CreateCommandBuffer();
    CreateSyncObjects();
}

void VulkanRenderer::Shutdown() {
    std::cout << "[VulkanRenderer] Arrêt du moteur.\n";

    // --- SÉCURITÉ CRUCIALE ---
    // On oblige le CPU à attendre que le GPU ait fini de dessiner sa dernière frame !
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }

    // Destruction de la Synchronisation
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
    }

    // --- LE FIX (Nettoyage) ---
    for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
        vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
    }

    // Destruction du Command Pool
    if (m_CommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    }

    // Le Pool détruit automatiquement tous les Descriptor Sets qu'il a créés !
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
    }

    vkDestroyPipeline(m_Device, m_SkyboxPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_SkyboxPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_SkyboxDescriptorSetLayout, nullptr);

    // Nettoyage des matériaux (Buffers VRAM)
    for (auto& [id, mat] : m_EntityMaterials) {
        DestroyVulkanMaterial(mat);
    }
    m_EntityMaterials.clear();

    if (m_DefaultTexture) {
        vkDestroySampler(m_Device, m_DefaultTexture->Sampler, nullptr);
        vkDestroyImageView(m_Device, m_DefaultTexture->View, nullptr);
        vkDestroyImage(m_Device, m_DefaultTexture->Image, nullptr);
        vkFreeMemory(m_Device, m_DefaultTexture->Memory, nullptr);
        delete m_DefaultTexture;
    }

    // Destruction des Framebuffers
    for (auto framebuffer : m_SwapChainFramebuffers) {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }

    // Destruction du Pipeline
    if (m_GraphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
    if (m_PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);

    if (m_DescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);

    // Destruction du Render Pass
    if (m_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
    }

    // Destruction des Image Views
    for (auto imageView : m_SwapChainImageViews) {
        vkDestroyImageView(m_Device, imageView, nullptr);
    }

    // Destruction de la Swapchain
    if (m_SwapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
    }

    // Destruction du Device Logique
    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, nullptr);
    }

    if (m_Surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    }

    // Vulkan détruit les objets dans le sens inverse de leur création
    if (m_Instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_Instance, nullptr);
    }
}

void VulkanRenderer::SubmitPushConstant(const glm::mat4& matrix) {
    if (m_IsFrameStarted && m_PipelineLayout != VK_NULL_HANDLE) {
        vkCmdPushConstants(m_CommandBuffers[m_CurrentFrame], m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &matrix);
    }
}
// ==============================================================================
// --- ÉTAPE 1 : L'INSTANCE VULKAN ---
// ==============================================================================
void VulkanRenderer::CreateInstance() {
    if (m_EnableValidationLayers && !CheckValidationLayerSupport()) {
        throw std::runtime_error("Erreur: Les Validation Layers Vulkan sont demandes, mais non disponibles !");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Cool Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Cool Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // --- ON FAIT CONFIANCE À GLFW ---
    if (!glfwVulkanSupported()) {
        throw std::runtime_error("Erreur fatale: GLFW ne detecte pas le chargeur Vulkan !");
    }

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    if (glfwExtensions == nullptr) {
        throw std::runtime_error("Erreur fatale: GLFW n'a pas trouve les extensions Wayland/X11 !");
    }

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (m_EnableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (m_EnableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
        createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer l'Instance Vulkan !");
    }

    std::cout << "[Vulkan] Instance creee avec succes.\n";
}

bool VulkanRenderer::CheckValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : m_ValidationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) return false;
    }
    return true;
}

// ==============================================================================
// --- ÉTAPE 1.5 : LA SURFACE DE FENÊTRE (Le pont avec l'OS) ---
// ==============================================================================
void VulkanRenderer::CreateSurface() {
    GLFWwindow* window = Application::Get().GetWindow();

    // --- SÉCURITÉ 1 : La fenêtre existe-t-elle ? ---
    if (window == nullptr) {
        throw std::runtime_error("Erreur fatale: La fenetre GLFW est NULL ! Elle n'a pas ete creee avant Vulkan !");
    }

    // --- SÉCURITÉ 2 : Code d'erreur Vulkan ---
    VkResult result = glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface);
    if (result != VK_SUCCESS) {
        std::cout << "[Vulkan] Erreur lors de la creation de la surface. Code VkResult: " << result << "\n";
        throw std::runtime_error("Erreur fatale: Impossible de creer la surface de fenetre Vulkan !");
    }

    std::cout << "[Vulkan] Surface de fenetre (GLFW) creee avec succes.\n";
}

// ==============================================================================
// --- ÉTAPE 2 : LA SÉLECTION DU GPU (Physical Device) ---
// ==============================================================================
void VulkanRenderer::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("Erreur fatale: Aucun GPU supportant Vulkan n'a ete trouve !");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    std::cout << "[Vulkan] " << deviceCount << " GPU(s) detecte(s) sur le systeme.\n";

    // On cherche le meilleur GPU (La carte dédiée en priorité)
    for (const auto& device : devices) {
        if (IsDeviceSuitable(device)) {
            m_PhysicalDevice = device;
            break;
        }
    }

    // Fallback : Si on n'a pas trouvé de carte dédiée, on prend le premier GPU disponible
    if (m_PhysicalDevice == VK_NULL_HANDLE) {
        std::cout << "[Vulkan] Avertissement: Aucune carte dediee trouvee. Utilisation du GPU par defaut.\n";
        m_PhysicalDevice = devices[0];
    }

    // --- AFFICHAGE DU VAINQUEUR ---
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &deviceProperties);
    std::cout << "[Vulkan] GPU selectionne : === " << deviceProperties.deviceName << " ===\n";
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    // Plus tard, on vérifiera ici si le GPU supporte le RayTracing (VK_KHR_ray_tracing_pipeline)
    // Pour l'instant, on veut juste s'assurer qu'on sélectionne la VRAIE carte graphique (NVIDIA/AMD)
    // et non pas la puce intégrée au processeur (Intel HD).

    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        return true;
    }

    return false;
}

// ==============================================================================
// --- ÉTAPE 3 : LE DEVICE LOGIQUE ET LES FILES D'ATTENTE ---
// ==============================================================================
bool VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device, uint32_t& outGraphics, uint32_t& outPresent) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    bool foundGraphics = false;
    bool foundPresent = false;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        // 1. Est-ce que cette porte supporte le dessin ?
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            outGraphics = i;
            foundGraphics = true;
        }

        // 2. Est-ce que cette porte supporte l'affichage sur notre Surface GLFW ?
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
        if (presentSupport) {
            outPresent = i;
            foundPresent = true;
        }

        if (foundGraphics && foundPresent) return true;
    }
    return false;
}

void VulkanRenderer::CreateLogicalDevice() {
    // 1. On trouve les numéros des portes (Souvent c'est la même porte pour les deux sur les RTX)
    if (!FindQueueFamilies(m_PhysicalDevice, m_GraphicsQueueFamilyIndex, m_PresentQueueFamilyIndex)) {
        throw std::runtime_error("Erreur: GPU incompatible, impossible de trouver les files d'attente requises !");
    }

    // 2. On configure la création des files d'attente (on utilise un 'set' pour éviter de créer deux fois la même file si Graphics = Present)
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { m_GraphicsQueueFamilyIndex, m_PresentQueueFamilyIndex };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // 3. Fonctionnalités matérielles (Laissé vide pour l'instant)
    VkPhysicalDeviceFeatures deviceFeatures{};

    // 4. Création du Device
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (m_EnableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
        createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // 5. L'appel système !
    if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Device Logique !");
    }

    // 6. On récupère les "poignées" des files d'attente
    vkGetDeviceQueue(m_Device, m_GraphicsQueueFamilyIndex, 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, m_PresentQueueFamilyIndex, 0, &m_PresentQueue);

    std::cout << "[Vulkan] Device Logique cree. Files d'attente connectees (Graphics: "
              << m_GraphicsQueueFamilyIndex << ", Present: " << m_PresentQueueFamilyIndex << ").\n";
}

// ==============================================================================
// --- ÉTAPE 4 : LA SWAPCHAIN (L'usine à Images) ---
// ==============================================================================
SwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    // 1. Les capacités de base (taille min/max des images)
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities);

    // 2. Les formats de couleur supportés (ex: RGBA 8-bit, HDR 10-bit)
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.formats.data());
    }

    // 3. Les modes de présentation (V-Sync, Uncapped, etc.)
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    // On cherche le format idéal : couleurs 8-bit standards (B8G8R8A8) en espace sRGB
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    // Si on ne trouve pas, on prend le premier qui vient
    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    // Mode MAILBOX = "Triple Buffering" (Uncapped FPS, très faible latence, pas de déchirure d'écran)
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    // Mode FIFO = "V-Sync" standard (60 FPS bloqués, toujours supporté)
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    // 1. Sur Linux, une fenêtre qui s'ouvre peut avoir une taille de 0x0 pendant une milliseconde.
    // On oblige le programme à attendre que l'OS donne de vraies dimensions !
    int width = 0, height = 0;
    glfwGetFramebufferSize(Application::Get().GetWindow(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(Application::Get().GetWindow(), &width, &height);
        glfwWaitEvents();
    }

    // 2. Si l'OS nous donne une taille valide, on l'utilise
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max() && capabilities.currentExtent.width > 0) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        // On borne les valeurs pour respecter les limites de la carte graphique
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

void VulkanRenderer::CreateSwapChain() {
    SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

    // Combien d'images on veut dans la file d'attente ? (Min + 1 pour faire du double/triple buffering)
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // On va dessiner des couleurs dessus !

    // Comment les files d'attentes partagent ces images ?
    uint32_t queueFamilyIndices[] = {m_GraphicsQueueFamilyIndex, m_PresentQueueFamilyIndex};
    if (m_GraphicsQueueFamilyIndex != m_PresentQueueFamilyIndex) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;

    // On cherche un mode de transparence de fenêtre supporté par le système d'exploitation
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (swapChainSupport.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if (swapChainSupport.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else if (swapChainSupport.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    } else {
        compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    createInfo.compositeAlpha = compositeAlpha;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    // ==========================================================
    // 1. CRÉATION AVEC DIAGNOSTIC EXACT !
    // ==========================================================
    VkResult result = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain);

    if (result != VK_SUCCESS) {
        std::cout << "\n[Vulkan] ERREUR CRITIQUE: Echec de la Swapchain !\n";
        std::cout << "[Vulkan] Code d'erreur VkResult : " << result << "\n\n";
        throw std::runtime_error("Erreur fatale: Impossible de creer la Swapchain !");
    }

    // 2. RÉCUPÉRATION DES IMAGES DANS LA RAM DU GPU
    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
    m_SwapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

    // 3. SAUVEGARDE DE LA CONFIGURATION
    m_SwapChainImageFormat = surfaceFormat.format;
    m_SwapChainExtent = extent;

    std::cout << "[Vulkan] Swapchain creee avec " << imageCount << " images (Resolution: "
              << extent.width << "x" << extent.height << ").\n";
}

// ==============================================================================
// --- ÉTAPE 5 : LES IMAGE VIEWS (Les "lunettes" pour lire la VRAM) ---
// ==============================================================================
void VulkanRenderer::CreateImageViews() {
    // On redimensionne notre tableau pour qu'il ait exactement la même taille que celui des images
    m_SwapChainImageViews.resize(m_SwapChainImages.size());

    for (size_t i = 0; i < m_SwapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_SwapChainImages[i];

        // C'est une image 2D classique
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_SwapChainImageFormat;

        // On ne mélange pas les couleurs (le rouge reste rouge, etc.)
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // On définit l'objectif de l'image (C'est pour y mettre des couleurs, pas de la profondeur)
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        // On crée la vue !
        if (vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Erreur fatale: Impossible de creer une Image View !");
        }
    }

    std::cout << "[Vulkan] " << m_SwapChainImageViews.size() << " Image Views creees avec succes.\n";
}

// ==============================================================================
// --- ÉTAPE 6 : LE RENDER PASS (Le plan de vol de l'image) ---
// ==============================================================================
void VulkanRenderer::CreateRenderPass() {
    // 1. Description de la "Cible" (L'image de la Swapchain)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_SwapChainImageFormat; // Même format que nos images
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // Pas d'anti-aliasing MSAA pour l'instant
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Au début de la frame : on efface l'image (le fameux fond noir)
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // A la fin : on garde le résultat en mémoire pour l'afficher
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    // La transformation magique de l'image :
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // On se fiche de ce qu'il y avait avant sur l'image
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // A la fin du rendu, elle DOIT être prête pour l'écran !

    // 2. La référence à cette cible pour notre "Sous-passe" de rendu
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0; // Index 0 dans notre futur tableau
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Format ultra-optimisé pendant qu'on dessine

    // 3. La Sous-passe (Ici on fait un rendu graphique standard)
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // 4. Dépendance de synchronisation (Crucial !)
    // On dit à Vulkan : "Attends que l'écran ait fini de lire l'ancienne image avant d'écrire la nouvelle dessus"
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // 5. Création finale de l'objet
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Render Pass !");
    }

    std::cout << "[Vulkan] Render Pass cree avec succes.\n";
}

// ==============================================================================
// --- ÉTAPE 7 : LES FRAMEBUFFERS (Les toiles de peinture) ---
// ==============================================================================
void VulkanRenderer::CreateFramebuffers() {
    // On prépare un tableau de la même taille que nos images (ex: 4)
    m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size());

    // On crée un Framebuffer pour chaque Image View
    for (size_t i = 0; i < m_SwapChainImageViews.size(); i++) {

        // Les "Attachments" correspondent à ce qu'on a défini dans le Render Pass.
        // Ici, on a un seul attachment (la couleur), qui est l'Image View actuelle.
        VkImageView attachments[] = {
            m_SwapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass; // On lie le Framebuffer au contrat du Render Pass
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_SwapChainExtent.width;   // Largeur de la fenêtre
        framebufferInfo.height = m_SwapChainExtent.height; // Hauteur de la fenêtre
        framebufferInfo.layers = 1; // 1 seule couche (ce n'est pas de la VR stéréoscopique)

        // Création !
        if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Erreur fatale: Impossible de creer un Framebuffer !");
        }
    }

    std::cout << "[Vulkan] " << m_SwapChainFramebuffers.size() << " Framebuffers crees avec succes.\n";
}

// ==============================================================================
// --- ÉTAPE 8 : COMMAND POOL ET COMMAND BUFFER (Le carnet et le stylo) ---
// ==============================================================================
void VulkanRenderer::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    // FLAG MAGIQUE : Permet de réinitialiser notre Command Buffer à chaque frame (60 fois par seconde)
    // sans avoir à détruire et recréer la mémoire entière du Pool.
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    // On précise que ces commandes sont destinées à la file d'attente graphique (Dessin)
    poolInfo.queueFamilyIndex = m_GraphicsQueueFamilyIndex;

    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Command Pool !");
    }
    std::cout << "[Vulkan] Command Pool cree avec succes.\n";
}

void VulkanRenderer::CreateCommandBuffer() {
    m_CommandBuffers.resize(MAX_FRAMES_IN_FLIGHT); // On prépare la place pour 2 stylos

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size(); // On alloue les 2 !

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible d'allouer les Command Buffers !");
    }
    std::cout << "[Vulkan] Command Buffers alloues avec succes.\n";
}

// ==============================================================================
// --- ÉTAPE 9 : SYNCHRONISATION (Les feux tricolores CPU/GPU) ---
// ==============================================================================
void VulkanRenderer::CreateSyncObjects() {
    m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    // --- LE FIX EST ICI : On utilise MAX_FRAMES_IN_FLIGHT ---
    m_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // UNE SEULE BOUCLE pour tout le monde :
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Erreur fatale: Impossible de creer les objets de synchronisation CPU !");
            }
    }

    std::cout << "[Vulkan] Objets de synchronisation crees avec succes.\n";
    std::cout << "\n[Vulkan] === INITIALISATION TERMINEE ===\n";
}

// --------------------------------------------------------------

// ==============================================================================
// --- ÉTAPE 10 : LA BOUCLE DE RENDU (Le dessin !) ---
// ==============================================================================
void VulkanRenderer::Clear() {
    BeginFrameIfNeeded();

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

    if (m_TargetFramebuffer) {
        // --- 1. RENDU HORS-ÉCRAN (Viewport 3D) ---
        renderPassInfo.renderPass = m_TargetFramebuffer->GetRenderPass();
        renderPassInfo.framebuffer = m_TargetFramebuffer->GetVulkanFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = { m_TargetFramebuffer->GetSpecification().Width, m_TargetFramebuffer->GetSpecification().Height };

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // Gris très sombre pour l'éditeur
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_CommandBuffers[m_CurrentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)m_TargetFramebuffer->GetSpecification().Width;
        viewport.height = (float)m_TargetFramebuffer->GetSpecification().Height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = renderPassInfo.renderArea.extent;
        vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);

    } else {
        // --- 2. RENDU PRINCIPAL (Swapchain) ---
        renderPassInfo.renderPass = m_RenderPass;
        renderPassInfo.framebuffer = m_SwapChainFramebuffers[m_CurrentImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_SwapChainExtent;

        VkClearValue clearColor = {{{0.1f, 0.2f, 0.4f, 1.0f}}}; // Le fond bleu !
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(m_CommandBuffers[m_CurrentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)m_SwapChainExtent.width;
        viewport.height = (float)m_SwapChainExtent.height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = m_SwapChainExtent;
        vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
    }
}

void VulkanRenderer::BeginFrameIfNeeded() {
    if (!m_IsFrameStarted) {
        vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);
        vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_CurrentImageIndex);
        vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);
        vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &beginInfo);

        m_IsFrameStarted = true;
    }
}

void VulkanRenderer::EndScene() {
    vkCmdEndRenderPass(m_CommandBuffers[m_CurrentFrame]);

    // Si le target est nul, cela veut dire qu'on vient de finir la Swapchain Principale !
    // Il est donc temps d'envoyer tout le carnet à la carte graphique.
    if (!m_TargetFramebuffer) {
        vkEndCommandBuffer(m_CommandBuffers[m_CurrentFrame]);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_CurrentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];

        VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_CurrentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]);

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = {m_SwapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        vkQueuePresentKHR(m_PresentQueue, &presentInfo);

        m_IsFrameStarted = false; // On reset pour la frame suivante !
        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}

void VulkanRenderer::BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
    if (!m_IsFrameStarted) return;

    // --- SAUVEGARDE ET CORRECTION VULKAN ---
    m_SceneViewMatrix = view;
    m_SceneProjectionMatrix = projection;
    m_SceneProjectionMatrix[1][1] *= -1.0f; // On inverse l'axe Y de la projection pour correspondre à Vulkan !

    m_CameraPos = cameraPos;

    VkRenderPassBeginInfo renderPassInfo{};

    // On lie le pipeline global (notre configuration de shaders) au stylo !
    vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

}

void VulkanRenderer::DrawGrid(bool enable) {
    // Rendu de la grille
}

void VulkanRenderer::RenderScene(Scene* scene, int renderMode) {
    if (!m_IsFrameStarted || !scene) return;

    // On demande à EnTT de nous donner toutes les entités avec un Transform ET un Mesh
    auto view = scene->m_Registry.view<TransformComponent, MeshComponent>();

    // --- 1. DESSIN DE LA SKYBOX ---
    vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyboxPipeline);

    struct SkyboxPush {
        glm::mat4 view;
        glm::mat4 proj;
    } skyPush{};
    skyPush.view = m_SceneViewMatrix;
    skyPush.proj = m_SceneProjectionMatrix;

    vkCmdPushConstants(m_CommandBuffers[m_CurrentFrame], m_SkyboxPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SkyboxPush), &skyPush);
    vkCmdBindDescriptorSets(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyboxPipelineLayout, 0, 1, &m_SkyboxDescriptorSet, 0, nullptr);

    m_SkyboxCube->Draw();

    vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

    for (auto entityID : view) {
        Entity entity{ entityID, scene };
        auto& meshComp = entity.GetComponent<MeshComponent>();

        // On vérifie que les données 3D sont bien chargées
        if (meshComp.MeshData) {

            // 1. On récupère la Matrice de l'entité et la Matrice de la Caméra
            glm::mat4 modelMatrix = scene->GetWorldTransform(entity);
            glm::mat4 viewProjMatrix = m_SceneProjectionMatrix * m_SceneViewMatrix;

            // 2. On les emballe dans une structure temporaire (exactement comme dans le shader .vert)
            struct PushConstants {
                glm::mat4 model;
                glm::mat4 viewProj;
            } push{};

            push.model = modelMatrix;
            push.viewProj = viewProjMatrix;

            // 3. On envoie nos 128 octets directement dans le carnet de commandes !
            vkCmdPushConstants(m_CommandBuffers[m_CurrentFrame], m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);

            // --- NOUVEAU : GESTION DU MATÉRIAU INDIVIDUEL ---
            if (m_EntityMaterials.find(entityID) == m_EntityMaterials.end()) {
                VulkanTexture* t_albedo = m_DefaultWhiteTexture;

                if (entity.HasComponent<MaterialComponent>()) {
                    auto& matComp = entity.GetComponent<MaterialComponent>();
                    if (!matComp.Textures.empty()) {
                        t_albedo = static_cast<VulkanTexture*>(matComp.Textures.begin()->second);
                    }
                }

                m_EntityMaterials[entityID] = CreateVulkanMaterial(t_albedo, m_DefaultNormalTexture, m_DefaultWhiteTexture, m_DefaultWhiteTexture, m_DefaultWhiteTexture);
            }

            // On récupère le matériau de CE mesh
            VulkanMaterial& mat = m_EntityMaterials[entityID];

            MaterialUBO ubo{};
            if (entity.HasComponent<ColorComponent>()) {
                ubo.baseColor = glm::vec4(entity.GetComponent<ColorComponent>().Color, 1.0f);
            } else {
                ubo.baseColor = glm::vec4(0.8f, 0.2f, 0.3f, 1.0f);
            }

            // --- ON ENVOIE LA CAMÉRA ---
            ubo.cameraPos = glm::vec4(m_CameraPos, 1.0f);

            // Paramètres PBR
            ubo.metallic = 0.5f;
            ubo.roughness = 0.2f;
            ubo.ao = 1.0f;

            // On copie la couleur dans le Buffer privé du matériau
            memcpy(mat.UniformBuffersMapped[m_CurrentFrame], &ubo, sizeof(ubo));

            // On copie la couleur dans le Buffer privé du matériau
            memcpy(mat.UniformBuffersMapped[m_CurrentFrame], &ubo, sizeof(ubo));

            // On donne le Descriptor Set exclusif au GPU !
            vkCmdBindDescriptorSets(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_PipelineLayout, 0, 1, &mat.DescriptorSets[m_CurrentFrame], 0, nullptr);

            // 4. Ordre de dessin Vulkan
            meshComp.MeshData->Draw();
        }
    }
}

void VulkanRenderer::SetShadowResolution(uint32_t resolution) {
    // Redimensionnement des Framebuffers d'ombres
}

// ==============================================================================
// --- ÉTAPE 11 : L'INTÉGRATION IMGUI ---
// ==============================================================================
void VulkanRenderer::InitImGui(GLFWwindow* window) {
    // 1. Création de l'armoire à mémoire (Descriptor Pool) requise par ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_ImGuiPool) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Descriptor Pool d'ImGui !");
    }

    // 2. Branchement au backend officiel ImGui Vulkan
    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_Instance;
    init_info.PhysicalDevice = m_PhysicalDevice;
    init_info.Device = m_Device;
    init_info.QueueFamily = m_GraphicsQueueFamilyIndex;
    init_info.Queue = m_GraphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_ImGuiPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.ImageCount = static_cast<uint32_t>(m_SwapChainImages.size());
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    init_info.RenderPass = m_RenderPass;

    // Note : Dans les versions récentes de vcpkg, les polices sont envoyées au GPU automatiquement
    ImGui_ImplVulkan_Init(&init_info);
}

void VulkanRenderer::BeginImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void VulkanRenderer::EndImGuiFrame() {
    ImGui::Render();
    // On injecte les commandes de dessin ImGui directement dans le carnet de notre Frame active !
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CommandBuffers[m_CurrentFrame]);
}

void VulkanRenderer::ShutdownImGui() {
    // Sécurité avant destruction
    vkDeviceWaitIdle(m_Device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    vkDestroyDescriptorPool(m_Device, m_ImGuiPool, nullptr);
}

VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data()); // Vulkan veut des pointeurs sur 32 bits !

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le module de shader !");
    }
    return shaderModule;
}

// ==============================================================================
// --- ÉTAPE 12 : LE PIPELINE GRAPHIQUE (La configuration matérielle) ---
// ==============================================================================
void VulkanRenderer::CreateGraphicsPipeline() {
    // 1. Chargement des Shaders SPIR-V
    auto vertShaderCode = ReadFile("shaders/triangle_vert.spv");
    auto fragShaderCode = ReadFile("shaders/triangle_frag.spv");

    VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

    // Configuration de l'étape "Vertex"
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main"; // Le point d'entrée du shader

    // Configuration de l'étape "Fragment" (Pixels)
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // --- LE FIX DE LA GÉOMÉTRIE : ON DIT À VULKAN COMMENT LIRE UN 'Vertex' ---
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex); // La taille totale d'un point
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    // 0: Position (vec3 = R32G32B32)
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, Position);
    // 1: Normale (vec3)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, Normal);
    // 2: TexCoords (vec2 = R32G32)
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, TexCoords);
    // 3: Tangent (vec3)
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Vertex, Tangent);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 4. Assemblage (On veut dessiner des triangles complets)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 5. Fenêtre d'affichage (Viewport)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; // Un seul écran dynamique
    viewportState.scissorCount = 1;

    // 6. Rastériseur (Cull face, mode plein, etc.)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; // On ne dessine pas l'arrière des faces
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 7. Multi-échantillonnage (Antialiasing - Désactivé pour l'instant)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 8. Mélange des couleurs (Alpha Blending - Opaque classique)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE; // On remplace la couleur, on ne la mélange pas

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 9. Layout du Pipeline (Push Constants ET Descriptor Sets)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) * 2;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    // On attache notre fameux plan de colis (Descriptor Set Layout)
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;

    // On attache les Push Constants
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Pipeline Layout !");
    }

    // --- LE FIX DE LA PROFONDEUR (Z-Buffer) ---
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // --- LE GRAND FINAL ---
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_PipelineLayout;

    pipelineInfo.renderPass = m_SceneRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Graphics Pipeline !");
    }

    std::cout << "[Vulkan] Graphics Pipeline cree avec succes.\n";

    // On détruit les modules de shaders, la carte graphique a déjà ingéré leur code !
    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

// ==============================================================================
// --- UTILITAIRES MÉMOIRE ET COMMANDES (Pour Framebuffers et Textures) ---
// ==============================================================================

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Erreur fatale: Impossible de trouver un type de memoire GPU adequat !");
}

VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // On envoie l'ordre immédiat et on oblige le processeur à attendre que ce soit fini
    vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_GraphicsQueue);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
}

void VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Echec de la creation du buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Echec de l'allocation de la memoire du buffer!");
    }
    vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);
}

void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Layout transition non supportee !");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::CreateSceneRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthAttachmentRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_SceneRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Echec Scene RenderPass");
    }
}

void VulkanRenderer::CreateDescriptorSetLayout() {
    // 1. Le contrat pour nos variables mathématiques (Couleurs, paramètres)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // Utilisé par le shader de pixels

    // 2. Le contrat pour notre Texture Principale
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // 3. On assemble le plan PBR
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0] = uboLayoutBinding; // UBO

    // Albedo, Normal, Metallic, Roughness, AO (Bindings 1 à 5)
    for (int i = 1; i <= 5; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].pImmutableSamplers = nullptr;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Descriptor Set Layout !");
    }

    std::cout << "[Vulkan] Descriptor Set Layout cree avec succes.\n";
}

void VulkanRenderer::CreateDescriptorPool() {
    // L'usine a maintenant 2 rayons : un pour les Buffers, un pour les Textures
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1000;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 5000; // 5 textures par set * 1000 sets !

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); // <--- On passe bien la taille du tableau (2)
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1000;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer le Descriptor Pool !");
    }
}

VulkanMaterial VulkanRenderer::CreateVulkanMaterial(VulkanTexture* albedo, VulkanTexture* normal, VulkanTexture* metallic, VulkanTexture* roughness, VulkanTexture* ao) {
    VulkanMaterial mat;
    mat.UniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    mat.UniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    mat.UniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
    mat.DescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    VkDeviceSize bufferSize = sizeof(MaterialUBO);

    // 1. Allocation des Buffers propres à CE matériau
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     mat.UniformBuffers[i], mat.UniformBuffersMemory[i]);
        vkMapMemory(m_Device, mat.UniformBuffersMemory[i], 0, bufferSize, 0, &mat.UniformBuffersMapped[i]);
    }

    // 2. Allocation de ses propres Descriptor Sets
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_DescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool; // On tire dans l'usine globale
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, mat.DescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Erreur: Impossible d'allouer les Descriptor Sets du Materiau !");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mat.UniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialUBO);

        // On prépare les 5 infos d'images
        std::array<VkDescriptorImageInfo, 5> imageInfos{};
        VulkanTexture* textures[] = { albedo, normal, metallic, roughness, ao };

        for(int t = 0; t < 5; t++) {
            imageInfos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[t].imageView = textures[t] ? textures[t]->View : m_DefaultWhiteTexture->View;
            imageInfos[t].sampler = textures[t] ? textures[t]->Sampler : m_DefaultWhiteTexture->Sampler;
        }

        std::array<VkWriteDescriptorSet, 6> descriptorWrites{};

        // UBO (Binding 0)
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mat.DescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // Textures (Bindings 1 à 5)
        for(int t = 0; t < 5; t++) {
            descriptorWrites[t + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[t + 1].dstSet = mat.DescriptorSets[i];
            descriptorWrites[t + 1].dstBinding = t + 1;
            descriptorWrites[t + 1].dstArrayElement = 0;
            descriptorWrites[t + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[t + 1].descriptorCount = 1;
            descriptorWrites[t + 1].pImageInfo = &imageInfos[t];
        }

        vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    return mat;
}

void VulkanRenderer::DestroyVulkanMaterial(VulkanMaterial& mat) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkUnmapMemory(m_Device, mat.UniformBuffersMemory[i]);
        vkDestroyBuffer(m_Device, mat.UniformBuffers[i], nullptr);
        vkFreeMemory(m_Device, mat.UniformBuffersMemory[i], nullptr);
    }
    // Pas besoin de détruire les Descriptor Sets, détruire le Pool global suffit !
}

VulkanTexture* VulkanRenderer::CreateSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    VulkanTexture* tex = new VulkanTexture();
    unsigned char pixel[] = { r, g, b, a };
    VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_Device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixel, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_Device, stagingBufferMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 1;
    imageInfo.extent.height = 1;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // CORRECTION : On utilise bien 'tex' partout ici !
    vkCreateImage(m_Device, &imageInfo, nullptr, &tex->Image);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, tex->Image, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_Device, &allocInfo, nullptr, &tex->Memory);
    vkBindImageMemory(m_Device, tex->Image, tex->Memory, 0);

    TransitionImageLayout(tex->Image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(stagingBuffer, tex->Image, 1, 1);
    TransitionImageLayout(tex->Image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex->Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(m_Device, &viewInfo, nullptr, &tex->View);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    vkCreateSampler(m_Device, &samplerInfo, nullptr, &tex->Sampler);

    return tex;
}

void VulkanRenderer::CreateSkyboxPipeline() {
    // 1. Le Plan (Layout) : Uniquement notre texture HDR !
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_SkyboxDescriptorSetLayout);

    // 2. Allocation de la mémoire et branchement de la texture HDR
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_SkyboxDescriptorSetLayout;
    vkAllocateDescriptorSets(m_Device, &allocInfo, &m_SkyboxDescriptorSet);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_SkyboxTexture ? m_SkyboxTexture->View : m_DefaultWhiteTexture->View;
    imageInfo.sampler = m_SkyboxTexture ? m_SkyboxTexture->Sampler : m_DefaultWhiteTexture->Sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_SkyboxDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);

    // 3. Configuration du Pipeline Layout (Pour les 2 matrices)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) * 2;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_SkyboxDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_SkyboxPipelineLayout);

    // 4. Les Shaders
    auto vertCode = ReadFile("shaders/skybox_vert.spv");
    auto fragCode = ReadFile("shaders/skybox_frag.spv");
    VkShaderModule vertModule = CreateShaderModule(vertCode);
    VkShaderModule fragModule = CreateShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

    // 5. États Fixes (Culling & Depth spécifiques au Ciel !)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, Position);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, Normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, TexCoords);

    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Vertex, Tangent);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; // <--- On regarde l'intérieur du cube
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; // <--- Le ciel ne bloque rien
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // <--- Z=1.0 passe le test !

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_SkyboxPipelineLayout;
    pipelineInfo.renderPass = m_SceneRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_SkyboxPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Erreur: Impossible de creer le pipeline Skybox !");
    }

    vkDestroyShaderModule(m_Device, fragModule, nullptr);
    vkDestroyShaderModule(m_Device, vertModule, nullptr);
}