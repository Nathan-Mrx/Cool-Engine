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
#include "DDGIVolume.h"

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

    // =========================================================================
    // LE FIX EST ICI : On charge les fonctions RT dès que le Device est prêt !
    // =========================================================================
    vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(m_Device, "vkCreateAccelerationStructureKHR");
    vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(m_Device, "vkDestroyAccelerationStructureKHR");
    vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(m_Device, "vkGetAccelerationStructureBuildSizesKHR");
    vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(m_Device, "vkGetAccelerationStructureDeviceAddressKHR");
    vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(m_Device, "vkCmdBuildAccelerationStructuresKHR");

    if (!vkCreateAccelerationStructureKHR || !vkCmdBuildAccelerationStructuresKHR) {
        throw std::runtime_error("Erreur fatale: Impossible de charger les fonctions KHR pour le Ray Tracing !");
    }
    // =========================================================================

    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateSceneRenderPass();
    CreateDescriptorSetLayout();
    CreateShadowResources();
    CreateShadowRenderPass();
    CreateShadowPipeline();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateDescriptorPool();
    CreateCommandPool();

    m_DefaultWhiteTexture = CreateSolidColorTexture(255, 255, 255, 255);
    m_DefaultBlackTexture = CreateSolidColorTexture(0, 0, 0, 255);
    m_DefaultNormalTexture = CreateSolidColorTexture(128, 128, 255, 255);

    GenerateBRDFLUT();

    m_SkyboxCube = PrimitiveFactory::CreateCube();
    m_SkyboxTexture = static_cast<VulkanTexture*>(TextureLoader::LoadHDR("assets/textures/sky.hdr"));

    GenerateEnvironmentCubemap();
    GenerateIrradianceCubemap();
    GeneratePrefilterCubemap();
    CreateDDGIPipeline();
    CreateSkyboxPipeline();
    CreateCommandBuffer();

    m_TLAS.resize(MAX_FRAMES_IN_FLIGHT);
    m_TLASInstancesBuffer.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    m_TLASInstancesMemory.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

    CreateSyncObjects();
}

void VulkanRenderer::Shutdown() {
    std::cout << "[VulkanRenderer] Arrêt du moteur.\n";

    // ==========================================================
    // 1. ARRÊT D'URGENCE (On attend que le GPU ait fini son travail)
    // ==========================================================
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }

    // ==========================================================
    // 2. NETTOYAGE DES RESSOURCES DU JEU (Objets, Matériaux, Textures)
    // ==========================================================

    // On force la destruction de notre primitive Skybox
    m_SkyboxCube.reset();

    // On vide les buffers VRAM de tous nos matériaux
    for (auto& [id, mat] : m_EntityMaterials) {
        DestroyVulkanMaterial(mat);
    }
    m_EntityMaterials.clear();

    // L'éboueur automatique pour TOUTES nos textures
    auto DestroyTex = [&](VulkanTexture* tex) {
        if (tex) {
            vkDestroySampler(m_Device, tex->Sampler, nullptr);
            vkDestroyImageView(m_Device, tex->View, nullptr);
            vkDestroyImage(m_Device, tex->Image, nullptr);
            vkFreeMemory(m_Device, tex->Memory, nullptr);
            delete tex;
        }
    };

    for (auto tex : m_TrackedTextures) {
        DestroyTex(tex);
    }
    m_TrackedTextures.clear();

    // On remet les pointeurs à 0 par sécurité
    m_DefaultWhiteTexture = nullptr;
    m_DefaultBlackTexture = nullptr;
    m_DefaultNormalTexture = nullptr;
    m_SkyboxTexture = nullptr;
    m_BrdfLutTexture = nullptr;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_TLASInstancesBuffer[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_TLASInstancesBuffer[i], nullptr);
            vkFreeMemory(m_Device, m_TLASInstancesMemory[i], nullptr);
        }
        // NOUVEAU
        if (m_TLASScratchBuffer[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_TLASScratchBuffer[i], nullptr);
            vkFreeMemory(m_Device, m_TLASScratchMemory[i], nullptr);
        }
        DestroyAccelerationStructure(m_TLAS[i]);
    }

    // ==========================================================
    // 3. NETTOYAGE DES OMBRES (CSM)
    // ==========================================================
    if (m_ShadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_ShadowSampler, nullptr);
    }
    for (auto view : m_ShadowCascadeViews) {
        vkDestroyImageView(m_Device, view, nullptr);
    }
    m_ShadowCascadeViews.clear();

    if (m_ShadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_ShadowImageView, nullptr);
    }
    if (m_ShadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_Device, m_ShadowImage, nullptr);
    }
    if (m_ShadowImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_ShadowImageMemory, nullptr);
    }
    for (auto fb : m_ShadowFramebuffers) {
        vkDestroyFramebuffer(m_Device, fb, nullptr);
    }
    m_ShadowFramebuffers.clear();

    if (m_ShadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_ShadowRenderPass, nullptr);
    }

    if (m_ShadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_ShadowPipeline, nullptr);
    }
    if (m_ShadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_ShadowPipelineLayout, nullptr);
    }

    // ==========================================================
    // 3. NETTOYAGE DES RENDER PASSES ET FRAMEBUFFERS
    // ==========================================================

    for (auto framebuffer : m_SwapChainFramebuffers) {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }
    m_SwapChainFramebuffers.clear();

    if (m_SceneRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_SceneRenderPass, nullptr);
    }

    // LE CORRECTIF EST ICI : On détruit aussi le Render Pass principal !
    if (m_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
    }

    // ==========================================================
    // 4. NETTOYAGE DES PIPELINES
    // ==========================================================

    vkDestroyPipeline(m_Device, m_SkyboxPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_SkyboxPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_SkyboxDescriptorSetLayout, nullptr);

    vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);

    vkDestroyPipeline(m_Device, m_DDGIPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_DDGIPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_DDGIDescriptorSetLayout, nullptr);

    // ==========================================================
    // 5. NETTOYAGE DES MÉMOIRES ET COMMANDES VULKAN
    // ==========================================================

    // LE CORRECTIF EST ICI : Une seule destruction propre pour l'usine à colis !
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
    }
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
    }

    if (m_CommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    }

    // ==========================================================
    // 6. NETTOYAGE DE LA SYNCHRONISATION (Feux tricolores)
    // ==========================================================

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
    }
    for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
        vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
    }

    // ==========================================================
    // 7. NETTOYAGE DES FONDATIONS (Swapchain, Fenêtre, OS)
    // ==========================================================

    for (auto imageView : m_SwapChainImageViews) {
        vkDestroyImageView(m_Device, imageView, nullptr);
    }
    m_SwapChainImageViews.clear();

    if (m_SwapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
    }

    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, nullptr);
    }

    if (m_Surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    }

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
    // 1. On trouve les numéros des portes
    if (!FindQueueFamilies(m_PhysicalDevice, m_GraphicsQueueFamilyIndex, m_PresentQueueFamilyIndex)) {
        throw std::runtime_error("Erreur: GPU incompatible, impossible de trouver les files d'attente requises !");
    }

    // 2. On configure la création des files d'attente
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

    // 3. Les features de base classiques
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    // 4. Les Features Vulkan 1.2 (Vital : Buffer Device Address)
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;
    vulkan12Features.descriptorIndexing = VK_TRUE;

    // 5. Les Features d'Acceleration Structure
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{};
    accelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelStructFeatures.accelerationStructure = VK_TRUE;
    accelStructFeatures.pNext = &vulkan12Features; // On attache la 1.2 ici

    // 6. Les Features Ray Query
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.rayQuery = VK_TRUE;
    rayQueryFeatures.pNext = &accelStructFeatures; // On attache l'Accel ici

    // 7. La capsule globale
    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};
    physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    physicalDeviceFeatures2.features = deviceFeatures;
    physicalDeviceFeatures2.pNext = &rayQueryFeatures; // Le début de la chaîne !

    // 8. Création du Device
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    // LE FIX EST LÀ : pEnabledFeatures = nullptr et pNext pointe vers la chaîne
    createInfo.pEnabledFeatures = nullptr;
    createInfo.pNext = &physicalDeviceFeatures2;

    // On active les extensions complètes (Swapchain + Ray Tracing)
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

    // Validation Layers
    if (m_EnableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
        createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // 9. L'appel système UNIQUE !
    if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de créer le Logical Device avec Ray Tracing !");
    }

    // 10. On récupère les "poignées" des files d'attente
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

    // --- LE FIX : Autant de sémaphores que d'images d'écran (4) ! ---
    m_RenderFinishedSemaphores.resize(m_SwapChainImages.size());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Erreur fatale: Impossible de creer les objets de synchronisation CPU !");
            }
    }

    // Boucle séparée pour les sémaphores de présentation
    for (size_t i = 0; i < m_SwapChainImages.size(); i++) {
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Erreur fatale: Impossible de creer les semaphores de presentation !");
        }
    }

    std::cout << "[Vulkan] Objets de synchronisation crees avec succes.\n";
    std::cout << "\n[Vulkan] === INITIALISATION TERMINEE ===\n";
}

// --------------------------------------------------------------

// ==============================================================================
// --- ÉTAPE 10 : LA BOUCLE DE RENDU (Le dessin !) ---
// ==============================================================================
void VulkanRenderer::Clear(Scene* scene) {
    // Si la frame n'est pas ouverte, on l'ouvre (utile pour le tout premier appel)
    BeginFrame();

    // Le TLAS est mis à jour ici car on est sûr que le carnet est ouvert,
    // et qu'aucun RenderPass n'a encore commencé.
    if (scene) {
        UpdateTLAS(scene);

        // =========================================================
        // --- LE DÉCLENCHEMENT DU COMPUTE SHADER DDGI EST ICI ! ---
        // =========================================================
        if (scene->GetDDGIVolume()) {
            ComputeDDGI(scene->GetDDGIVolume());
        }
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

    if (m_TargetFramebuffer) {
        renderPassInfo.renderPass = m_TargetFramebuffer->GetRenderPass();
        renderPassInfo.framebuffer = m_TargetFramebuffer->GetVulkanFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = { m_TargetFramebuffer->GetSpecification().Width, m_TargetFramebuffer->GetSpecification().Height };

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_CommandBuffers[m_CurrentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0.0f, 0.0f, (float)m_TargetFramebuffer->GetSpecification().Width, (float)m_TargetFramebuffer->GetSpecification().Height, 0.0f, 1.0f};
        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &viewport);
        VkRect2D scissor{{0,0}, renderPassInfo.renderArea.extent};
        vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
    } else {
        renderPassInfo.renderPass = m_RenderPass;
        renderPassInfo.framebuffer = m_SwapChainFramebuffers[m_CurrentImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_SwapChainExtent;

        VkClearValue clearColor = {{{0.1f, 0.2f, 0.4f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(m_CommandBuffers[m_CurrentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0.0f, 0.0f, (float)m_SwapChainExtent.width, (float)m_SwapChainExtent.height, 0.0f, 1.0f};
        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &viewport);
        VkRect2D scissor{{0,0}, m_SwapChainExtent};
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

    // On retire toute la soumission de la queue d'ici !
    // Si c'est un panneau ImGui ou la scène principale, ça ne fait que fermer le RenderPass actuel.
    // L'envoi global (Submit) se fera dans EndFrame().
}

void VulkanRenderer::BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
    if (!m_IsFrameStarted) return;

    m_SceneViewMatrix = view;
    m_SceneProjectionMatrix = projection;
    m_SceneProjectionMatrix[1][1] *= -1.0f;
    m_CameraPos = cameraPos;

    // On ne lie plus le Pipeline principal ici, car le RenderPass n'a pas encore commencé !
}

void VulkanRenderer::DrawGrid(bool enable) {
    // Rendu de la grille
}

void VulkanRenderer::RenderScene(Scene* scene, int renderMode) {
    if (!m_IsFrameStarted || !scene) return;

    // ==========================================================
    // LE DESSIN CLASSIQUE (Dans le Render Pass principal déjà ouvert par Clear())
    // On ne recalcule PLUS les ombres ici, PrepareShadows() l'a déjà fait !
    // ==========================================================

    // 1. La Skybox
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

    // 2. Les Entités PBR
    vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

    auto view = scene->m_Registry.view<TransformComponent, MeshComponent>();

    for (auto entityID : view) {
        Entity entity{ entityID, scene };
        auto& meshComp = entity.GetComponent<MeshComponent>();

        if (meshComp.MeshData) {
            glm::mat4 modelMatrix = scene->GetWorldTransform(entity);
            glm::mat4 viewProjMatrix = m_SceneProjectionMatrix * m_SceneViewMatrix;

            struct PushConstants {
                glm::mat4 model;
                glm::mat4 viewProj;
            } push{};

            push.model = modelMatrix;
            push.viewProj = viewProjMatrix;

            vkCmdPushConstants(m_CommandBuffers[m_CurrentFrame], m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);

            // ==========================================================
            // GESTION DU MATÉRIAU PBR DE L'ENTITÉ
            // ==========================================================

            VulkanTexture* texAlbedo = m_DefaultWhiteTexture;
            VulkanTexture* texNormal = m_DefaultNormalTexture;
            VulkanTexture* texMetallic = m_DefaultBlackTexture;
            VulkanTexture* texRoughness = m_DefaultWhiteTexture;
            VulkanTexture* texAO = m_DefaultWhiteTexture;

            glm::vec4 baseColor = glm::vec4(1.0f); // Blanc par défaut pour ne pas teinter la texture
            float metallicVal = 0.0f;
            float roughnessVal = 0.5f;
            float aoVal = 1.0f;

            if (entity.HasComponent<ColorComponent>()) {
                baseColor = glm::vec4(entity.GetComponent<ColorComponent>().Color, 1.0f);
            }

            if (entity.HasComponent<MaterialComponent>()) {
                auto& matComp = entity.GetComponent<MaterialComponent>();

                if (matComp.TextureOverrides.count("u_Albedo") && matComp.TextureOverrides["u_Albedo"])
                    texAlbedo = static_cast<VulkanTexture*>(matComp.TextureOverrides["u_Albedo"]);

                if (matComp.TextureOverrides.count("u_Normal") && matComp.TextureOverrides["u_Normal"])
                    texNormal = static_cast<VulkanTexture*>(matComp.TextureOverrides["u_Normal"]);

                if (matComp.TextureOverrides.count("u_Metallic") && matComp.TextureOverrides["u_Metallic"])
                    texMetallic = static_cast<VulkanTexture*>(matComp.TextureOverrides["u_Metallic"]);

                if (matComp.TextureOverrides.count("u_Roughness") && matComp.TextureOverrides["u_Roughness"])
                    texRoughness = static_cast<VulkanTexture*>(matComp.TextureOverrides["u_Roughness"]);

                if (matComp.TextureOverrides.count("u_AO") && matComp.TextureOverrides["u_AO"])
                    texAO = static_cast<VulkanTexture*>(matComp.TextureOverrides["u_AO"]);

                if (matComp.FloatOverrides.count("u_Metallic")) metallicVal = matComp.FloatOverrides["u_Metallic"];
                else if (texMetallic != m_DefaultBlackTexture) metallicVal = 1.0f;

                if (matComp.FloatOverrides.count("u_Roughness")) roughnessVal = matComp.FloatOverrides["u_Roughness"];
                else if (texRoughness != m_DefaultWhiteTexture) roughnessVal = 1.0f;

                if (matComp.FloatOverrides.count("u_AO")) aoVal = matComp.FloatOverrides["u_AO"];
                else if (texAO != m_DefaultWhiteTexture) aoVal = 1.0f;

                if (matComp.ColorOverrides.count("u_BaseColor")) baseColor = matComp.ColorOverrides["u_BaseColor"];
            }

            if (m_EntityMaterials.find(entityID) == m_EntityMaterials.end()) {
                m_EntityMaterials[entityID] = CreateVulkanMaterial(
                    texAlbedo, texNormal, texMetallic, texRoughness, texAO
                );
            }

            VulkanMaterial& mat = m_EntityMaterials[entityID];
            MaterialUBO ubo{};

            ubo.baseColor = baseColor;
            ubo.cameraPos = glm::vec4(m_CameraPos, 1.0f);
            ubo.metallic = metallicVal;
            ubo.roughness = roughnessVal;
            ubo.ao = aoVal;

            for (int j = 0; j < 4; j++) {
                ubo.lightSpaceMatrices[j] = m_CurrentShadowMatrices[j];
            }
            ubo.cascadeSplits = m_CurrentCascadeSplits;

            memcpy(mat.UniformBuffersMapped[m_CurrentFrame], &ubo, sizeof(ubo));

            // NOUVEAU : On s'assure que le TLAS de ce Descriptor Set est à jour !
            if (m_TLAS[m_CurrentFrame].handle != VK_NULL_HANDLE) {
                VkWriteDescriptorSetAccelerationStructureKHR descriptorAS{};
                descriptorAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                descriptorAS.accelerationStructureCount = 1;
                descriptorAS.pAccelerationStructures = &m_TLAS[m_CurrentFrame].handle;

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.pNext = &descriptorAS;
                write.dstSet = mat.DescriptorSets[m_CurrentFrame];
                write.dstBinding = 11;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

                vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
            }

            vkCmdBindDescriptorSets(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_PipelineLayout, 0, 1, &mat.DescriptorSets[m_CurrentFrame], 0, nullptr);

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

    // --- LE FIX RAY TRACING EST ICI ---
    // Si on demande une Device Address, on doit l'autoriser à l'allocation !
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &allocFlagsInfo;
    }

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Echec de l'allocation de la memoire du buffer!");
    }
    vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);
}

void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount) {
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
    barrier.subresourceRange.levelCount = mipLevels; // Support des Mips
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount; // Support des Cubemaps (6)

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
    }
    // NOUVEAU : Passage du mode Rendu (Attachment) vers mode Texture (Sampled)
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    // (Dans la fonction TransitionImageLayout, à rajouter :)
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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
    // 1. Le contrat UBO
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // 2. On assemble le plan PBR (Maintenant 12 éléments !)
    std::array<VkDescriptorSetLayoutBinding, 12> bindings{};
    bindings[0] = uboLayoutBinding;

    // Boucle de 1 à 10 pour les Textures
    for (int i = 1; i <= 10; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].pImmutableSamplers = nullptr;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    // 3. NOUVEAU : Le contrat pour le TLAS
    VkDescriptorSetLayoutBinding tlasLayoutBinding{};
    tlasLayoutBinding.binding = 11;
    tlasLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    tlasLayoutBinding.descriptorCount = 1;
    tlasLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // On le lira dans triangle.frag
    bindings[11] = tlasLayoutBinding;

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
    // L'usine a maintenant 4 rayons ! (On ajoute les Storage Images pour le Compute)
    std::array<VkDescriptorPoolSize, 4> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1000;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 5000;

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[2].descriptorCount = 1000;

    // --- NOUVEAU ---
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = 1000;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); // Passe à 4 !
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

        std::array<VkDescriptorImageInfo, 10> imageInfos{};
        VulkanTexture* textures[] = {
            albedo, normal, metallic, roughness, ao,
            m_EnvironmentCubemap, m_BrdfLutTexture, m_IrradianceCubemap, m_PrefilterCubemap
        };

        for(int t = 0; t < 9; t++) {
            imageInfos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[t].imageView = textures[t] ? textures[t]->View : m_DefaultWhiteTexture->View;
            imageInfos[t].sampler = textures[t] ? textures[t]->Sampler : m_DefaultWhiteTexture->Sampler;
        }

        imageInfos[9].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfos[9].imageView = m_ShadowImageView;
        imageInfos[9].sampler = m_ShadowSampler;

        // SEULEMENT 11 ÉCRITURES ! (On ne met PAS le TLAS ici)
        std::array<VkWriteDescriptorSet, 11> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mat.DescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        for(int t = 0; t < 10; t++) {
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

    m_TrackedTextures.push_back(tex);

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

void VulkanRenderer::GenerateBRDFLUT() {
    std::cout << "[Vulkan] Generation de la BRDF LUT en cours...\n";

    const uint32_t dim = 512;
    VkFormat format = VK_FORMAT_R16G16_SFLOAT;

    // 1. L'Image de destination (Elle va servir d'Attachment puis de Texture)
    m_BrdfLutTexture = new VulkanTexture();
    m_TrackedTextures.push_back(m_BrdfLutTexture);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = dim;
    imageInfo.extent.height = dim;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(m_Device, &imageInfo, nullptr, &m_BrdfLutTexture->Image);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_Device, m_BrdfLutTexture->Image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_BrdfLutTexture->Memory);
    vkBindImageMemory(m_Device, m_BrdfLutTexture->Image, m_BrdfLutTexture->Memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.image = m_BrdfLutTexture->Image;
    vkCreateImageView(m_Device, &viewInfo, nullptr, &m_BrdfLutTexture->View);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_BrdfLutTexture->Sampler);

    // 2. Le Render Pass Offscreen
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // Directement prêt pour la lecture !

    VkAttachmentReference colorReference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkRenderPass renderPass;
    vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &renderPass);

    // 3. Framebuffer
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &m_BrdfLutTexture->View;
    fbInfo.width = dim;
    fbInfo.height = dim;
    fbInfo.layers = 1;
    VkFramebuffer framebuffer;
    vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &framebuffer);

    // 4. Pipeline Minimaliste (Zéro géométrie en entrée)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

    auto vertCode = ReadFile("shaders/brdf_vert.spv");
    auto fragCode = ReadFile("shaders/brdf_frag.spv");
    VkShaderModule vertModule = CreateShaderModule(vertCode);
    VkShaderModule fragModule = CreateShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo emptyInputState{};
    emptyInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // On dessine tout !

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;
    colorBlendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &emptyInputState;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;

    VkPipeline pipeline;
    vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    // 5. Exécution de l'enregistrement !
    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass;
    rpBegin.framebuffer = framebuffer;
    rpBegin.renderArea.extent = {dim, dim};
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport{0.0f, 0.0f, (float)dim, (float)dim, 0.0f, 1.0f};
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
    VkRect2D scissor{{0,0}, {dim, dim}};
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    vkCmdDraw(cmdBuf, 3, 1, 0, 0); // Le fameux triangle magique plein écran !

    vkCmdEndRenderPass(cmdBuf);
    EndSingleTimeCommands(cmdBuf);

    // 6. Nettoyage de l'usine éphémère (On garde juste l'image générée)
    vkDestroyPipeline(m_Device, pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, pipelineLayout, nullptr);
    vkDestroyShaderModule(m_Device, fragModule, nullptr);
    vkDestroyShaderModule(m_Device, vertModule, nullptr);
    vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    vkDestroyRenderPass(m_Device, renderPass, nullptr);

    std::cout << "[Vulkan] BRDF LUT generee avec succes !\n";
}

void VulkanRenderer::GenerateEnvironmentCubemap() {
    std::cout << "[Vulkan] Generation de l'Environment Cubemap (6 faces)...\n";

    const uint32_t dim = 512; // Résolution de notre ciel
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    // 1. CRÉATION DE L'IMAGE CUBEMAP
    m_EnvironmentCubemap = new VulkanTexture();
    m_TrackedTextures.push_back(m_EnvironmentCubemap);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = dim;
    imageInfo.extent.height = dim;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6; // <--- Les 6 faces !
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // <--- Le flag magique !
    vkCreateImage(m_Device, &imageInfo, nullptr, &m_EnvironmentCubemap->Image);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_Device, m_EnvironmentCubemap->Image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_EnvironmentCubemap->Memory);
    vkBindImageMemory(m_Device, m_EnvironmentCubemap->Image, m_EnvironmentCubemap->Memory, 0);

    // Vue globale en tant que CUBE pour les shaders
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE; // <--- C'est un cube !
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6; // On englobe les 6 faces
    viewInfo.image = m_EnvironmentCubemap->Image;
    vkCreateImageView(m_Device, &viewInfo, nullptr, &m_EnvironmentCubemap->View);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_EnvironmentCubemap->Sampler);

    // 2. VUES INDIVIDUELLES ET RENDER PASS (Pour dessiner face par face)
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkRenderPass renderPass;
    vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &renderPass);

    // Création de 6 Vues 2D temporaires et 6 Framebuffers (Un par face)
    std::vector<VkImageView> faceViews(6);
    std::vector<VkFramebuffer> framebuffers(6);
    for (int i = 0; i < 6; i++) {
        VkImageViewCreateInfo faceViewInfo = viewInfo;
        faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // Vue 2D classique
        faceViewInfo.subresourceRange.baseArrayLayer = i; // On cible UNE seule face !
        faceViewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(m_Device, &faceViewInfo, nullptr, &faceViews[i]);

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &faceViews[i];
        fbInfo.width = dim;
        fbInfo.height = dim;
        fbInfo.layers = 1;
        vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &framebuffers[i]);
    }

    // 3. PIPELINE ET DESCRIPTOR SET (On injecte m_SkyboxTexture !)
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    VkDescriptorSetLayout descriptorSetLayout;
    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfoDesc{};
    allocInfoDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoDesc.descriptorPool = m_DescriptorPool;
    allocInfoDesc.descriptorSetCount = 1;
    allocInfoDesc.pSetLayouts = &descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(m_Device, &allocInfoDesc, &descriptorSet);

    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = m_SkyboxTexture->View;
    descImageInfo.sampler = m_SkyboxTexture->Sampler;
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;
    vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) * 2; // Vue + Proj
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

    // Création du Pipeline (Exactement comme ta Skybox, mais on vise 'renderPass' et on garde Culling = NONE)
    auto vertCode = ReadFile("shaders/cubemap_vert.spv");
    auto fragCode = ReadFile("shaders/equirect_to_cube_frag.spv");
    VkShaderModule vertModule = CreateShaderModule(vertCode);
    VkShaderModule fragModule = CreateShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}
    };

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, Position); // On a juste besoin de la position !

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // On dessine l'intérieur

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE; // Pas besoin de test de profondeur

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;

    VkPipeline pipeline;
    vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    // 4. LES 6 CAMÉRAS ET LE RENDU
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    captureProjection[1][1] *= -1.0f; // Correction Vulkan

    // Matrices Z-Up (X, -X, Y, -Y, Z, -Z)
    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)), // Top
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)), // Bottom
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();

    VkViewport viewport{0.0f, 0.0f, (float)dim, (float)dim, 0.0f, 1.0f};
    VkRect2D scissor{{0,0}, {dim, dim}};

    for (int i = 0; i < 6; i++) {
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[i]; // On cible la face i !
        rpBegin.renderArea.extent = {dim, dim};
        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        struct PushConstants {
            glm::mat4 view;
            glm::mat4 proj;
        } push{};
        push.view = captureViews[i];
        push.proj = captureProjection;

        vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        // On dessine le cube !
        VkBuffer vertexBuffers[] = { m_SkyboxCube->GetVertexBuffer() }; // On le stocke dans un tableau local (Obligatoire en C++)
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(cmdBuf, m_SkyboxCube->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // On utilise la bonne fonction GetIndicesCount() !
        vkCmdDrawIndexed(cmdBuf, m_SkyboxCube->GetIndicesCount(), 1, 0, 0, 0);

        vkCmdEndRenderPass(cmdBuf);
    }

    EndSingleTimeCommands(cmdBuf);

    // 5. NETTOYAGE DES DÉCHETS DE CONSTRUCTION
    vkDestroyPipeline(m_Device, pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, pipelineLayout, nullptr);
    vkDestroyShaderModule(m_Device, fragModule, nullptr);
    vkDestroyShaderModule(m_Device, vertModule, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, descriptorSetLayout, nullptr);
    vkDestroyRenderPass(m_Device, renderPass, nullptr);

    for (int i = 0; i < 6; i++) {
        vkDestroyFramebuffer(m_Device, framebuffers[i], nullptr);
        vkDestroyImageView(m_Device, faceViews[i], nullptr);
    }

    std::cout << "[Vulkan] Cubemap genere avec succes !\n";
}

void VulkanRenderer::GenerateIrradianceCubemap() {
    std::cout << "[Vulkan] Generation de l'Irradiance Cubemap (32x32)...\n";

    const uint32_t dim = 32; // <--- Résolution minuscule, parfaite pour la lumière ambiante
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    m_IrradianceCubemap = new VulkanTexture();
    m_TrackedTextures.push_back(m_IrradianceCubemap);

    // 1. CRÉATION DE L'IMAGE CUBEMAP
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = dim;
    imageInfo.extent.height = dim;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    vkCreateImage(m_Device, &imageInfo, nullptr, &m_IrradianceCubemap->Image);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_Device, m_IrradianceCubemap->Image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_IrradianceCubemap->Memory);
    vkBindImageMemory(m_Device, m_IrradianceCubemap->Image, m_IrradianceCubemap->Memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;
    viewInfo.image = m_IrradianceCubemap->Image;
    vkCreateImageView(m_Device, &viewInfo, nullptr, &m_IrradianceCubemap->View);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_IrradianceCubemap->Sampler);

    // 2. RENDER PASS ET FRAMEBUFFERS
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkRenderPass renderPass;
    vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &renderPass);

    std::vector<VkImageView> faceViews(6);
    std::vector<VkFramebuffer> framebuffers(6);
    for (int i = 0; i < 6; i++) {
        VkImageViewCreateInfo faceViewInfo = viewInfo;
        faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        faceViewInfo.subresourceRange.baseArrayLayer = i;
        faceViewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(m_Device, &faceViewInfo, nullptr, &faceViews[i]);

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &faceViews[i];
        fbInfo.width = dim;
        fbInfo.height = dim;
        fbInfo.layers = 1;
        vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &framebuffers[i]);
    }

    // 3. DESCRIPTOR SET (On lit l'Environment Cubemap généré juste avant !)
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    VkDescriptorSetLayout descriptorSetLayout;
    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfoDesc{};
    allocInfoDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoDesc.descriptorPool = m_DescriptorPool;
    allocInfoDesc.descriptorSetCount = 1;
    allocInfoDesc.pSetLayouts = &descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(m_Device, &allocInfoDesc, &descriptorSet);

    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = m_EnvironmentCubemap->View;   // <--- ON LIT LE CUBE
    descImageInfo.sampler = m_EnvironmentCubemap->Sampler;
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;
    vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) * 2;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

    // SHADERS (On utilise Irradiance !)
    auto vertCode = ReadFile("shaders/cubemap_vert.spv");
    auto fragCode = ReadFile("shaders/irradiance_frag.spv"); // <--- LE NOUVEAU SHADER
    VkShaderModule vertModule = CreateShaderModule(vertCode);
    VkShaderModule fragModule = CreateShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}
    };

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, Position);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;

    VkPipeline pipeline;
    vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    // 4. RENDU DES 6 FACES
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    captureProjection[1][1] *= -1.0f;

    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();
    VkViewport viewport{0.0f, 0.0f, (float)dim, (float)dim, 0.0f, 1.0f};
    VkRect2D scissor{{0,0}, {dim, dim}};

    for (int i = 0; i < 6; i++) {
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[i];
        rpBegin.renderArea.extent = {dim, dim};
        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        struct PushConstants { glm::mat4 view; glm::mat4 proj; } push{};
        push.view = captureViews[i];
        push.proj = captureProjection;

        vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        VkBuffer vertexBuffers[] = { m_SkyboxCube->GetVertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmdBuf, m_SkyboxCube->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmdBuf, m_SkyboxCube->GetIndicesCount(), 1, 0, 0, 0);

        vkCmdEndRenderPass(cmdBuf);
    }

    EndSingleTimeCommands(cmdBuf);

    // 5. NETTOYAGE
    vkDestroyPipeline(m_Device, pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, pipelineLayout, nullptr);
    vkDestroyShaderModule(m_Device, fragModule, nullptr);
    vkDestroyShaderModule(m_Device, vertModule, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, descriptorSetLayout, nullptr);
    vkDestroyRenderPass(m_Device, renderPass, nullptr);
    for (int i = 0; i < 6; i++) {
        vkDestroyFramebuffer(m_Device, framebuffers[i], nullptr);
        vkDestroyImageView(m_Device, faceViews[i], nullptr);
    }

    std::cout << "[Vulkan] Irradiance Cubemap (Diffuse) generée avec succes !\n";
}

void VulkanRenderer::GeneratePrefilterCubemap() {
    std::cout << "[Vulkan] Generation du Prefilter Cubemap (5 MipMaps)...\n";

    const uint32_t dim = 128; // La résolution de base de notre reflets nets
    const uint32_t mipLevels = 5;
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    m_PrefilterCubemap = new VulkanTexture();
    m_TrackedTextures.push_back(m_PrefilterCubemap);

    // 1. IMAGE AVEC 5 MIPMAPS
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = dim;
    imageInfo.extent.height = dim;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels; // <--- On active 5 niveaux !
    imageInfo.arrayLayers = 6;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    vkCreateImage(m_Device, &imageInfo, nullptr, &m_PrefilterCubemap->Image);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_Device, m_PrefilterCubemap->Image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_PrefilterCubemap->Memory);
    vkBindImageMemory(m_Device, m_PrefilterCubemap->Image, m_PrefilterCubemap->Memory, 0);

    // Vue globale Cubemap
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;
    viewInfo.image = m_PrefilterCubemap->Image;
    vkCreateImageView(m_Device, &viewInfo, nullptr, &m_PrefilterCubemap->View);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR; // <--- Lecture fluide entre les mips !
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = (float)mipLevels;
    vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_PrefilterCubemap->Sampler);

    // 2. RENDER PASS
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkRenderPass renderPass;
    vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &renderPass);

    // 3. PIPELINE ET DESCRIPTOR
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    VkDescriptorSetLayout descriptorSetLayout;
    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfoDesc{};
    allocInfoDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoDesc.descriptorPool = m_DescriptorPool;
    allocInfoDesc.descriptorSetCount = 1;
    allocInfoDesc.pSetLayouts = &descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(m_Device, &allocInfoDesc, &descriptorSet);

    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = m_EnvironmentCubemap->View; // On lit le cube source net !
    descImageInfo.sampler = m_EnvironmentCubemap->Sampler;
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;
    vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);

    // Push constants plus gros ! (View, Proj, et Roughness !)
    struct PushConstants {
        glm::mat4 view;
        glm::mat4 proj;
        float roughness;
    };
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

    auto vertCode = ReadFile("shaders/cubemap_vert.spv");
    auto fragCode = ReadFile("shaders/prefilter_frag.spv"); // <--- SHADER PREFILTER
    VkShaderModule vertModule = CreateShaderModule(vertCode);
    VkShaderModule fragModule = CreateShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}
    };

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, Position);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;

    VkPipeline pipeline;
    vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    // 4. RENDU DES 5 NIVEAUX x 6 FACES
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    captureProjection[1][1] *= -1.0f;

    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();

    std::vector<VkImageView> tempViews;
    std::vector<VkFramebuffer> tempFramebuffers;

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        // Le niveau de flou grandit, la résolution diminue (128 -> 64 -> 32...)
        uint32_t mipWidth  = static_cast<uint32_t>(dim * std::pow(0.5, mip));
        uint32_t mipHeight = static_cast<uint32_t>(dim * std::pow(0.5, mip));

        VkViewport viewport{0.0f, 0.0f, (float)mipWidth, (float)mipHeight, 0.0f, 1.0f};
        VkRect2D scissor{{0,0}, {mipWidth, mipHeight}};

        float roughness = (float)mip / (float)(mipLevels - 1);

        for (uint32_t i = 0; i < 6; ++i) {
            // Création d'une vue et d'un framebuffer éphémères pour CE mipmap et CETTE face
            VkImageViewCreateInfo faceViewInfo = viewInfo;
            faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            faceViewInfo.subresourceRange.baseMipLevel = mip;
            faceViewInfo.subresourceRange.levelCount = 1;
            faceViewInfo.subresourceRange.baseArrayLayer = i;
            faceViewInfo.subresourceRange.layerCount = 1;
            VkImageView faceView;
            vkCreateImageView(m_Device, &faceViewInfo, nullptr, &faceView);
            tempViews.push_back(faceView);

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &faceView;
            fbInfo.width = mipWidth;
            fbInfo.height = mipHeight;
            fbInfo.layers = 1;
            VkFramebuffer framebuffer;
            vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &framebuffer);
            tempFramebuffers.push_back(framebuffer);

            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = renderPass;
            rpBegin.framebuffer = framebuffer;
            rpBegin.renderArea.extent = {mipWidth, mipHeight};
            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearColor;

            vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
            vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

            PushConstants push{};
            push.view = captureViews[i];
            push.proj = captureProjection;
            push.roughness = roughness; // On envoie le niveau de rugosité !

            vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            VkBuffer vertexBuffers[] = { m_SkyboxCube->GetVertexBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmdBuf, m_SkyboxCube->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmdBuf, m_SkyboxCube->GetIndicesCount(), 1, 0, 0, 0);

            vkCmdEndRenderPass(cmdBuf);
        }
    }

    EndSingleTimeCommands(cmdBuf);

    // 5. NETTOYAGE
    vkDestroyPipeline(m_Device, pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, pipelineLayout, nullptr);
    vkDestroyShaderModule(m_Device, fragModule, nullptr);
    vkDestroyShaderModule(m_Device, vertModule, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, descriptorSetLayout, nullptr);
    vkDestroyRenderPass(m_Device, renderPass, nullptr);

    for (auto fb : tempFramebuffers) vkDestroyFramebuffer(m_Device, fb, nullptr);
    for (auto view : tempViews) vkDestroyImageView(m_Device, view, nullptr);

    std::cout << "[Vulkan] Prefilter Cubemap genere avec succes !\n";
}

void VulkanRenderer::CreateShadowResources() {
    std::cout << "[Vulkan] Creation des ressources CSM (" << SHADOW_MAP_CASCADE_COUNT << " cascades, " << SHADOW_MAP_RESOLUTION << "px)...\n";

    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; // Précision maximale

    // 1. IMAGE ARRAY (Une seule texture contenant les 4 cascades)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = SHADOW_MAP_RESOLUTION;
    imageInfo.extent.height = SHADOW_MAP_RESOLUTION;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = SHADOW_MAP_CASCADE_COUNT; // <--- C'est ici qu'on fait l'Array !
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    vkCreateImage(m_Device, &imageInfo, nullptr, &m_ShadowImage);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_Device, m_ShadowImage, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_ShadowImageMemory);
    vkBindImageMemory(m_Device, m_ShadowImage, m_ShadowImageMemory, 0);

    // 2. VUE GLOBALE (Pour le shader PBR : type 2D_ARRAY)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_ShadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // <--- Vue Array
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = SHADOW_MAP_CASCADE_COUNT;
    vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ShadowImageView);

    // 3. VUES INDIVIDUELLES (Pour écrire dedans face par face)
    m_ShadowCascadeViews.resize(SHADOW_MAP_CASCADE_COUNT);
    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        VkImageViewCreateInfo cascadeViewInfo = viewInfo;
        cascadeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // <--- Vue 2D simple
        cascadeViewInfo.subresourceRange.baseArrayLayer = i;
        cascadeViewInfo.subresourceRange.layerCount = 1; // On ne cible qu'une seule couche !
        vkCreateImageView(m_Device, &cascadeViewInfo, nullptr, &m_ShadowCascadeViews[i]);
    }

    // 4. SAMPLER HARDWARE (Avec comparaison native pour ombres douces)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; // Hors champ = Pas d'ombre
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // Blanc = Éclairé !
    vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_ShadowSampler);
}

void VulkanRenderer::CreateShadowRenderPass() {
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_D32_SFLOAT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // On doit le garder pour le shader !
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; // Prêt à être lu par le PBR

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0; // Pas de couleur, que de la profondeur !
    subpass.pDepthStencilAttachment = &depthRef;

    // Dépendance cruciale : on doit avoir fini d'écrire l'ombre avant de lire dans le fragment shader
    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_ShadowRenderPass);

    // 5. CRÉATION DES 4 FRAMEBUFFERS
    m_ShadowFramebuffers.resize(SHADOW_MAP_CASCADE_COUNT);
    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_ShadowRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &m_ShadowCascadeViews[i]; // On cible une couche spécifique
        fbInfo.width = SHADOW_MAP_RESOLUTION;
        fbInfo.height = SHADOW_MAP_RESOLUTION;
        fbInfo.layers = 1;
        vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &m_ShadowFramebuffers[i]);
    }
}

void VulkanRenderer::CreateShadowPipeline() {
    std::cout << "[Vulkan] Creation du Pipeline des Ombres (Depth-Only)...\n";

    auto vertCode = ReadFile("shaders/shadow_vert.spv");
    VkShaderModule vertModule = CreateShaderModule(vertCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertModule;
    vertShaderStageInfo.pName = "main";

    // Vertex Input : Seulement la Position ! Pas besoin d'UV, de Normales ou de Tangentes.
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = offsetof(Vertex, Position);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // RASTERIZER : Les "Hacks" physiques des ombres
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;         // <-- Anti Shadow-Acne

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Push Constant pour envoyer la matrice du Soleil
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_ShadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Erreur: Impossible de creer le Pipeline Layout des Ombres !");
    }

    // Paramètres dynamiques (Très pratique pour changer le biais et la zone de rendu à la volée)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // On assemble le Pipeline complet !
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;                  // <--- SEULEMENT 1 SHADER (Vertex) !
    pipelineInfo.pStages = &vertShaderStageInfo;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = nullptr;      // <--- ZÉRO GESTION DE COULEUR !
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_ShadowPipelineLayout;
    pipelineInfo.renderPass = m_ShadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_ShadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Erreur: Impossible de creer le Pipeline des Ombres !");
    }

    vkDestroyShaderModule(m_Device, vertModule, nullptr);
}

std::array<glm::mat4, VulkanRenderer::SHADOW_MAP_CASCADE_COUNT> VulkanRenderer::CalculateCascadeMatrices() {
    std::array<glm::mat4, SHADOW_MAP_CASCADE_COUNT> matrices;

    // Direction EXACTE du soleil de ton triangle.frag
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 0.5f, 1.0f));

    // Tes paliers de distance (en cm)
    float cascadeSplits[4] = { 1000.0f, 3000.0f, 7000.0f, 15000.0f };
    float lastSplit = 10.0f; // Near plane de ta caméra
    float aspect = (float)m_SwapChainExtent.width / (float)m_SwapChainExtent.height;

    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        glm::mat4 camProj = glm::perspective(glm::radians(45.0f), aspect, lastSplit, cascadeSplits[i]);
        camProj[1][1] *= -1.0f; // Correction Vulkan

        glm::mat4 invCam = glm::inverse(camProj * m_SceneViewMatrix);

        // On utilise -1.0 et 1.0 partout pour forcer l'extraction du cube de vue complet
        glm::vec3 frustumCorners[8] = {
            glm::vec3(-1.0f,  1.0f, -1.0f), glm::vec3( 1.0f,  1.0f, -1.0f), glm::vec3( 1.0f, -1.0f, -1.0f), glm::vec3(-1.0f, -1.0f, -1.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f), glm::vec3( 1.0f,  1.0f,  1.0f), glm::vec3( 1.0f, -1.0f,  1.0f), glm::vec3(-1.0f, -1.0f,  1.0f)
        };

        // 1. Trouver le vrai centre géographique de cette tranche de la vue
        glm::vec3 frustumCenter = glm::vec3(0.0f);
        for (int j = 0; j < 8; j++) {
            glm::vec4 pt = invCam * glm::vec4(frustumCorners[j], 1.0f);
            frustumCorners[j] = glm::vec3(pt) / pt.w;
            frustumCenter += frustumCorners[j];
        }
        frustumCenter /= 8.0f;

        // 2. "Bounding Sphere" : Rayon qui englobe cette vue
        float radius = 0.0f;
        for (int j = 0; j < 8; j++) {
            float distance = glm::length(frustumCorners[j] - frustumCenter);
            radius = std::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f; // L'arrondi magique pour la stabilité !

        glm::vec3 maxExtents = glm::vec3(radius);
        glm::vec3 minExtents = -maxExtents;

        // 3. Placer la caméra du soleil
        glm::mat4 lightView = glm::lookAt(frustumCenter + lightDir * radius, frustumCenter, glm::vec3(0.0f, 0.0f, 1.0f));

        // 4. "Z-Blocker" : On recule le plan de projection de 150 mètres en arrière !
        // Ça permet d'attraper les arbres qui sont derrière toi mais dont l'ombre devrait être visible devant toi.
        float zBlockerDistance = 15000.0f;
        glm::mat4 lightProj = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, minExtents.z - zBlockerDistance, maxExtents.z + zBlockerDistance);
        lightProj[1][1] *= -1.0f; // Flip Vulkan

        // 5. "Texel Snapping" : Annuler le tremblement des pixels d'ombre
        glm::mat4 shadowMatrix = lightProj * lightView;
        glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin *= (SHADOW_MAP_RESOLUTION / 2.0f);

        glm::vec4 roundedOrigin = glm::round(shadowOrigin);
        glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
        roundOffset = roundOffset * (2.0f / SHADOW_MAP_RESOLUTION);
        roundOffset.z = 0.0f;
        roundOffset.w = 0.0f;

        lightProj[3] += roundOffset;

        matrices[i] = lightProj * lightView;
        lastSplit = cascadeSplits[i];
    }

    return matrices;
}

void VulkanRenderer::RenderShadows(VkCommandBuffer cmdBuf, Scene* scene, const std::array<glm::mat4, SHADOW_MAP_CASCADE_COUNT>& cascadeMatrices) {
    if (!scene) return;

    VkViewport viewport{0.0f, 0.0f, (float)SHADOW_MAP_RESOLUTION, (float)SHADOW_MAP_RESOLUTION, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION}};

    // On récupère toutes tes entités 3D
    auto view = scene->m_Registry.view<TransformComponent, MeshComponent>();

    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_ShadowRenderPass;
        rpBegin.framebuffer = m_ShadowFramebuffers[i]; // On cible la cascade N°i
        rpBegin.renderArea.extent = { SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION };

        VkClearValue clearDepth;
        clearDepth.depthStencil = { 1.0f, 0 }; // 1.0f = le plus loin possible
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearDepth;

        vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline);
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        // Hack AAA : Le biais dynamique (Evite le Shadow Acne sur les faces éclairées)
        vkCmdSetDepthBias(cmdBuf, 1.25f, 0.0f, 1.75f);

        // --- LA BOUCLE DE DESSIN ---
        for (auto entityID : view) {
            Entity entity{ entityID, scene };
            auto& meshComp = entity.GetComponent<MeshComponent>();

            if (meshComp.MeshData) {
                // 1. Matrice Modèle et Calcul MVP pour le Soleil
                glm::mat4 modelMatrix = scene->GetWorldTransform(entity);
                glm::mat4 mvp = cascadeMatrices[i] * modelMatrix;

                // 2. On envoie la matrice au Vertex Shader d'ombre
                vkCmdPushConstants(cmdBuf, m_ShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

                // 3. Dessin pur ! (Ton MeshData->Draw() marche ici car il va utiliser le m_ShadowPipeline actif !)
                meshComp.MeshData->Draw();
            }
        }

        vkCmdEndRenderPass(cmdBuf);
    }
}

void VulkanRenderer::PrepareShadows(Scene* scene) {
    if (!scene) return;
    BeginFrameIfNeeded(); // On s'assure d'avoir un stylo (cmdBuf) prêt

    // On calcule les boîtes
    m_CurrentShadowMatrices = CalculateCascadeMatrices();
    // On sauvegarde les distances (les mêmes que dans CalculateCascadeMatrices)
    m_CurrentCascadeSplits = glm::vec4(1000.0f, 3000.0f, 7000.0f, 15000.0f);

    // On exécute la passe d'ombre OFFSCREEN
    RenderShadows(m_CommandBuffers[m_CurrentFrame], scene, m_CurrentShadowMatrices);
}

void VulkanRenderer::RenderMaterialPreview(
    Mesh* mesh, VulkanFramebuffer* target,
    glm::mat4 model, glm::mat4 view, glm::mat4 proj, glm::vec3 camPos,
    VulkanTexture* albedo, VulkanTexture* normal, VulkanTexture* metallic, VulkanTexture* roughness, VulkanTexture* ao,
    glm::vec4 colorVal, float metallicVal, float roughnessVal, float aoVal)
{
    if (!mesh || !target) return;

    // 1. On lance un rendu isolé (Sécurité absolue contre les plantages de Frame)
    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = target->GetRenderPass();
    rpBegin.framebuffer = target->GetVulkanFramebuffer();
    rpBegin.renderArea.extent = { target->GetSpecification().Width, target->GetSpecification().Height };

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.15f, 0.15f, 0.15f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

    VkViewport viewport{0.0f, 0.0f, (float)target->GetSpecification().Width, (float)target->GetSpecification().Height, 0.0f, 1.0f};
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
    VkRect2D scissor{{0,0}, rpBegin.renderArea.extent};
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    // 2. Matrices (Caméra)
    struct PushConstants { glm::mat4 model; glm::mat4 viewProj; } push{};
    push.model = model;
    push.viewProj = proj * view;
    vkCmdPushConstants(cmdBuf, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);

    // 3. Fallbacks Textures
    VulkanTexture* t_albedo = albedo ? albedo : m_DefaultWhiteTexture;
    VulkanTexture* t_normal = normal ? normal : m_DefaultNormalTexture;
    VulkanTexture* t_metallic = metallic ? metallic : m_DefaultBlackTexture;
    VulkanTexture* t_roughness = roughness ? roughness : m_DefaultWhiteTexture;
    VulkanTexture* t_ao = ao ? ao : m_DefaultWhiteTexture;

    // 4. On gère l'Entité factice pour le "Colis" de Preview
    entt::entity previewEntity = (entt::entity)999999;
    if (m_EntityMaterials.find(previewEntity) == m_EntityMaterials.end()) {
        // On le crée une seule fois !
        m_EntityMaterials[previewEntity] = CreateVulkanMaterial(t_albedo, t_normal, t_metallic, t_roughness, t_ao);
    }

    VulkanMaterial& mat = m_EntityMaterials[previewEntity];

    // MISE À JOUR DYNAMIQUE : On modifie les textures à la volée sans saturer la RAM !
    std::array<VkDescriptorImageInfo, 10> imageInfos{};
    VulkanTexture* textures[] = {
        t_albedo, t_normal, t_metallic, t_roughness, t_ao,
        m_EnvironmentCubemap, m_BrdfLutTexture, m_IrradianceCubemap, m_PrefilterCubemap
    };

    for(int t = 0; t < 9; t++) {
        imageInfos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[t].imageView = textures[t] ? textures[t]->View : m_DefaultWhiteTexture->View;
        imageInfos[t].sampler = textures[t] ? textures[t]->Sampler : m_DefaultWhiteTexture->Sampler;
    }
    // L'Array de Shadow Maps
    imageInfos[9].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageInfos[9].imageView = m_ShadowImageView;
    imageInfos[9].sampler = m_ShadowSampler;

    std::array<VkWriteDescriptorSet, 10> descriptorWrites{};
    for(int t = 0; t < 10; t++) {
        descriptorWrites[t].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[t].dstSet = mat.DescriptorSets[0]; // On met à jour le set de la preview
        descriptorWrites[t].dstBinding = t + 1;
        descriptorWrites[t].dstArrayElement = 0;
        descriptorWrites[t].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[t].descriptorCount = 1;
        descriptorWrites[t].pImageInfo = &imageInfos[t];
    }
    // On applique les nouveaux câbles instantanément !
    vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

    // Mise à jour des valeurs du PBR
    MaterialUBO ubo{};
    ubo.baseColor = colorVal;
    ubo.cameraPos = glm::vec4(camPos, 1.0f);
    ubo.metallic = metallic ? 1.0f : metallicVal;
    ubo.roughness = roughness ? 1.0f : roughnessVal;
    ubo.ao = ao ? 1.0f : aoVal;

    memcpy(mat.UniformBuffersMapped[0], &ubo, sizeof(ubo));

    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &mat.DescriptorSets[0], 0, nullptr);

    // 5. On dessine !
    VkBuffer vertexBuffers[] = { mesh->GetVertexBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, mesh->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmdBuf, mesh->GetIndicesCount(), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmdBuf);
    EndSingleTimeCommands(cmdBuf);
}

void VulkanRenderer::InvalidateEntityMaterial(entt::entity entityID) {
    if (m_EntityMaterials.find(entityID) != m_EntityMaterials.end()) {

        // On copie (capture) le matériel par valeur pour la lambda de destruction
        VulkanMaterial oldMat = m_EntityMaterials[entityID];

        // On efface l'entrée de la map pour que RenderScene() en recrée un neuf
        m_EntityMaterials.erase(entityID);

        // On met la destruction physique dans la file d'attente !
        // Elle sera exécutée au début de la frame SUIVANTE (donc en toute sécurité)
        m_MainDeletionQueue.push_function([=, device = m_Device]() {
            for (size_t i = 0; i < oldMat.UniformBuffers.size(); i++) {
                vkDestroyBuffer(device, oldMat.UniformBuffers[i], nullptr);
                vkFreeMemory(device, oldMat.UniformBuffersMemory[i], nullptr);
            }
            // Note: Les Descriptor Sets de ce matériel spécifique ne sont pas détruits un par un,
            // ils sont rattachés au m_DescriptorPool qui est détruit globalement à la fin.
        });
    }
}

VkDeviceAddress VulkanRenderer::GetBufferDeviceAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(m_Device, &info);
}

// ==============================================================================
// RAY TRACING : CONSTRUCTION DU BLAS (Bottom-Level Acceleration Structure)
// ==============================================================================
AccelerationStructure VulkanRenderer::CreateBLAS(VkBuffer vertexBuffer, uint32_t vertexCount, VkBuffer indexBuffer, uint32_t indexCount) {
    AccelerationStructure as{};

    // 1. On décrit où sont les triangles dans la VRAM
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = GetBufferDeviceAddress(vertexBuffer);
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = vertexCount;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = GetBufferDeviceAddress(indexBuffer);

    // 2. On emballe ça dans une géométrie
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = triangles;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // Aide le GPU (pas de canal alpha à tester)

    // 3. On configure la construction
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; // Optimisé pour le rendu
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t maxPrimitiveCount = indexCount / 3;

    // 4. On demande au GPU "De quelle taille de buffer as-tu besoin pour ce BLAS ?"
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimitiveCount, &buildSizesInfo);

    // 5. On crée le Buffer qui va contenir l'arbre binaire
    CreateBuffer(
        buildSizesInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        as.buffer, as.memory
    );

    // 6. On crée l'objet vulkan BLAS par-dessus le buffer
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = as.buffer;
    createInfo.size = buildSizesInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &as.handle);

    // 7. Un buffer "brouillon" temporaire requis par le GPU pour faire ses calculs d'arbre
    VkBuffer scratchBuffer;
    VkDeviceMemory scratchMemory;
    CreateBuffer(
        buildSizesInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        scratchBuffer, scratchMemory
    );

    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = as.handle;
    buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(scratchBuffer);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = maxPrimitiveCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos[1] = { &buildRangeInfo };

    // 8. ON ORDONNE LA CONSTRUCTION ! (Sur la queue graphique)
    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();
    vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, pBuildRangeInfos);
    EndSingleTimeCommands(cmdBuf);

    // 9. Nettoyage du brouillon et récupération de l'adresse finale
    vkDestroyBuffer(m_Device, scratchBuffer, nullptr);
    vkFreeMemory(m_Device, scratchMemory, nullptr);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = as.handle;
    as.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Device, &addressInfo);

    return as;
}

void VulkanRenderer::DestroyAccelerationStructure(AccelerationStructure& as) {
    if (as.handle != VK_NULL_HANDLE) {
        vkDestroyAccelerationStructureKHR(m_Device, as.handle, nullptr);
        as.handle = VK_NULL_HANDLE;
    }
    if (as.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, as.buffer, nullptr);
        as.buffer = VK_NULL_HANDLE;
    }
    if (as.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, as.memory, nullptr);
        as.memory = VK_NULL_HANDLE;
    }
}

// ==============================================================================
// RAY TRACING : CONSTRUCTION DU TLAS (Top-Level Acceleration Structure)
// ==============================================================================
void VulkanRenderer::BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances) {
    if (instances.empty()) return;

    uint32_t frame = m_CurrentFrame;

    // ====================================================================
    // --- FIX DU CRASH : INITIALISATION SÉCURISÉE DES VECTEURS ---
    // Si les tableaux n'ont pas encore été redimensionnés, on le fait ici !
    // ====================================================================
    if (m_TLAS.size() != MAX_FRAMES_IN_FLIGHT) {
        m_TLAS.resize(MAX_FRAMES_IN_FLIGHT);
        m_TLASInstancesBuffer.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
        m_TLASInstancesMemory.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    }
    if (m_TLASScratchBuffer.size() != MAX_FRAMES_IN_FLIGHT) {
        m_TLASScratchBuffer.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
        m_TLASScratchMemory.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    }

    VkDeviceSize bufferSize = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();

    // 1. On nettoie l'ancien buffer d'instances de CETTE frame s'il existe
    if (m_TLASInstancesBuffer[frame] != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, m_TLASInstancesBuffer[frame], nullptr);
        vkFreeMemory(m_Device, m_TLASInstancesMemory[frame], nullptr);
    }

    // 2. Création du buffer d'instances visible par le CPU
    CreateBuffer(
        bufferSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_TLASInstancesBuffer[frame], m_TLASInstancesMemory[frame]
    );

    // 3. Copie des instances
    void* data;
    vkMapMemory(m_Device, m_TLASInstancesMemory[frame], 0, bufferSize, 0, &data);
    memcpy(data, instances.data(), bufferSize);
    vkUnmapMemory(m_Device, m_TLASInstancesMemory[frame]);

    // 4. Configuration de la géométrie RT
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = GetBufferDeviceAddress(m_TLASInstancesBuffer[frame]);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t primitiveCount = static_cast<uint32_t>(instances.size());

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &buildSizesInfo);

    // 5. Destruction de l'ancien TLAS de la frame courante
    if (m_TLAS[frame].handle != VK_NULL_HANDLE) {
        DestroyAccelerationStructure(m_TLAS[frame]);
    }

    // 6. Allocation du nouveau TLAS
    CreateBuffer(
        buildSizesInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_TLAS[frame].buffer, m_TLAS[frame].memory
    );

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_TLAS[frame].buffer;
    createInfo.size = buildSizesInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_TLAS[frame].handle);

    // ==============================================================================
    // 7. LE FAMEUX SCRATCH BUFFER (Brouillon) DE LA FRAME COURANTE
    // ==============================================================================
    if (m_TLASScratchBuffer[frame] != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, m_TLASScratchBuffer[frame], nullptr);
        vkFreeMemory(m_Device, m_TLASScratchMemory[frame], nullptr);
    }

    CreateBuffer(
        buildSizesInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_TLASScratchBuffer[frame], m_TLASScratchMemory[frame]
    );

    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = m_TLAS[frame].handle;
    buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_TLASScratchBuffer[frame]);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = primitiveCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos[1] = { &buildRangeInfo };

    // 8. Construction via le Command Buffer asynchrone (Pas d'attente CPU !)
    VkCommandBuffer cmdBuf = m_CommandBuffers[frame];
    vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, pBuildRangeInfos);

    // 9. Barrière de mémoire : on protège le Shader pour qu'il attende la fin du build
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    // 10. Sauvegarde de l'adresse de l'arbre
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_TLAS[frame].handle;
    m_TLAS[frame].deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Device, &addressInfo);
}
// ==============================================================================
// RAY TRACING : MISE À JOUR DU TLAS AVEC LA SCÈNE
// ==============================================================================
void VulkanRenderer::UpdateTLAS(Scene* scene) {
    if (!scene) return;

    std::vector<VkAccelerationStructureInstanceKHR> instances;

    // On parcourt toutes les entités qui ont un Mesh et un Transform
    auto view = scene->m_Registry.view<TransformComponent, MeshComponent>();

    uint32_t instanceId = 0;
    for (auto entity : view) {
        auto [transformComp, meshComp] = view.get<TransformComponent, MeshComponent>(entity);

        if (!meshComp.MeshData || !meshComp.MeshData->GetBLAS()) continue;

        // 1. On récupère la matrice 4x4 classique
        glm::mat4 transform = transformComp.GetTransform();

        // 2. On la convertit au format Vulkan RT (3x4 transposée)
        VkTransformMatrixKHR transformMatrix;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                // glm stocke en Column-Major, donc transform[col][row]
                transformMatrix.matrix[row][col] = transform[col][row];
            }
        }

        // 3. On crée l'instance pour cette entité
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform = transformMatrix;
        instance.instanceCustomIndex = instanceId; // Utile plus tard pour retrouver le matériau dans le shader !
        instance.mask = 0xFF;                      // L'objet est visible par tous les rayons
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR; // On désactive le backface culling pour les rayons par sécurité
        instance.accelerationStructureReference = meshComp.MeshData->GetBLAS()->deviceAddress;

        instances.push_back(instance);
        instanceId++;
    }

    // 4. S'il y a des objets, on demande au GPU de reconstruire la carte !
    if (!instances.empty()) {
        BuildTLAS(instances);
    }
}

void VulkanRenderer::PreRender(Scene* scene) {
    // On ne fait plus rien ici, on a trouvé une meilleure place.
}

void VulkanRenderer::BeginFrame() {
    if (m_IsFrameStarted) return; // Sécurité

    vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);
    vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_CurrentImageIndex);
    vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);
    vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &beginInfo);

    m_IsFrameStarted = true;
}

void VulkanRenderer::EndFrame() {
    if (!m_IsFrameStarted) return; // Sécurité

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

    VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_CurrentImageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Erreur : Impossible de soumettre le Command Buffer !");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {m_SwapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_CurrentImageIndex;

    vkQueuePresentKHR(m_PresentQueue, &presentInfo);

    m_IsFrameStarted = false;
    m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VulkanTexture* VulkanRenderer::CreateStorageTexture(uint32_t width, uint32_t height, VkFormat format) {
    VulkanTexture* tex = new VulkanTexture();

    // 1. Création de l'Image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // L'astuce est ici : STORAGE pour y écrire via Compute, SAMPLED pour la lire dans triangle.frag !
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateImage(m_Device, &imageInfo, nullptr, &tex->Image);

    // 2. Allocation de la VRAM
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, tex->Image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(m_Device, &allocInfo, nullptr, &tex->Memory);
    vkBindImageMemory(m_Device, tex->Image, tex->Memory, 0);

    // 3. Création de la Vue (ImageView)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex->Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(m_Device, &viewInfo, nullptr, &tex->View);

    // 4. Création du Sampler Linéaire
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    vkCreateSampler(m_Device, &samplerInfo, nullptr, &tex->Sampler);

    // 5. Transition de l'image vers LAYOUT_GENERAL (Obligatoire pour les Compute Shaders)
    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex->Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmdBuf);

    return tex;
}

void VulkanRenderer::DestroyTexture(VulkanTexture* tex) {
    if (tex) {
        vkDestroySampler(m_Device, tex->Sampler, nullptr);
        vkDestroyImageView(m_Device, tex->View, nullptr);
        vkDestroyImage(m_Device, tex->Image, nullptr);
        vkFreeMemory(m_Device, tex->Memory, nullptr);
        delete tex;
    }
}

void VulkanRenderer::CreateDDGIPipeline() {
    // 1. Le Contrat (Descriptor Set Layout)
    // Binding 0 : La texture d'Irradiance (Storage Image, Write Only)
    VkDescriptorSetLayoutBinding storageBinding{};
    storageBinding.binding = 0;
    storageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageBinding.descriptorCount = 1;
    storageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1 : La Skybox ou texture d'environnement (Sampler classique)
    VkDescriptorSetLayoutBinding envMapBinding{};
    envMapBinding.binding = 1;
    envMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    envMapBinding.descriptorCount = 1;
    envMapBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {storageBinding, envMapBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DDGIDescriptorSetLayout);

    // 2. Le Pipeline Layout (Push Constants pour uProbeCount)
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::ivec3); // On envoie ivec3(x, y, z)

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DDGIDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

    vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_DDGIPipelineLayout);

    // 3. Le Compute Pipeline
    // Note : Ton script CompileShaders.py transforme sûrement .comp en _comp.spv
    auto compShaderCode = ReadFile("shaders/ddgi_update_comp.spv");
    VkShaderModule compShaderModule = CreateShaderModule(compShaderCode);

    VkPipelineShaderStageCreateInfo compShaderStageInfo{};
    compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compShaderStageInfo.module = compShaderModule;
    compShaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = m_DDGIPipelineLayout;
    pipelineInfo.stage = compShaderStageInfo;

    if (vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_DDGIPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale : Impossible de creer le DDGI Compute Pipeline !");
    }

    vkDestroyShaderModule(m_Device, compShaderModule, nullptr);
    std::cout << "[Vulkan] DDGI Compute Pipeline cree avec succes.\n";
}

void VulkanRenderer::ComputeDDGI(DDGIVolume* volume) {
    if (!volume || !m_IsFrameStarted) return;

    // 1. Initialisation paresseuse du Descriptor Set (Une seule fois !)
    if (m_DDGIDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_DDGIDescriptorSetLayout;

        vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DDGIDescriptorSet);

        // On branche nos images !
        VkDescriptorImageInfo storageImageInfo{};
        storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Obligatoire pour Storage
        storageImageInfo.imageView = volume->GetIrradianceTexture()->View;
        storageImageInfo.sampler = volume->GetIrradianceTexture()->Sampler;

        VkWriteDescriptorSet writeStorage{};
        writeStorage.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeStorage.dstSet = m_DDGIDescriptorSet;
        writeStorage.dstBinding = 0;
        writeStorage.dstArrayElement = 0;
        writeStorage.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeStorage.descriptorCount = 1;
        writeStorage.pImageInfo = &storageImageInfo;

        VkDescriptorImageInfo envMapInfo{};
        envMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        envMapInfo.imageView = m_EnvironmentCubemap ? m_EnvironmentCubemap->View : m_DefaultWhiteTexture->View;
        envMapInfo.sampler = m_EnvironmentCubemap ? m_EnvironmentCubemap->Sampler : m_DefaultWhiteTexture->Sampler;

        VkWriteDescriptorSet writeEnv{};
        writeEnv.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeEnv.dstSet = m_DDGIDescriptorSet;
        writeEnv.dstBinding = 1;
        writeEnv.dstArrayElement = 0;
        writeEnv.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeEnv.descriptorCount = 1;
        writeEnv.pImageInfo = &envMapInfo;

        std::array<VkWriteDescriptorSet, 2> writes = {writeStorage, writeEnv};
        vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // 2. LE LANCEMENT DU CALCUL
    VkCommandBuffer cmdBuf = m_CommandBuffers[m_CurrentFrame];

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_DDGIPipeline);
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_DDGIPipelineLayout, 0, 1, &m_DDGIDescriptorSet, 0, nullptr);

    glm::ivec3 probes = volume->GetProbeCount();
    vkCmdPushConstants(cmdBuf, m_DDGIPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::ivec3), &probes);

    // Dans le shader, on a mis local_size = (8, 8, 1). Ce qui correspond à un "carré" de sonde.
    // Donc on demande au GPU de lancer autant de groupes qu'il y a de sondes !
    vkCmdDispatch(cmdBuf, probes.x, probes.y, probes.z);

    // 3. LA BARRIÈRE DE SÉCURITÉ (Crucial en AAA)
    // On dit au GPU : "Attends que le Compute Shader ait fini d'écrire dans l'Irradiance avant que le Fragment Shader le lise !"
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL; // On le laisse en General pour la prochaine frame
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = volume->GetIrradianceTexture()->Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmdBuf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Le producteur
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // Le consommateur
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );
}