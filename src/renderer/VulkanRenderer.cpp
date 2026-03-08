#include "VulkanRenderer.h"
#include "core/Application.h"

#include <stdexcept>
#include <cstring>


void VulkanRenderer::Init() {
    std::cout << "\n=========================================\n";
    std::cout << "[VulkanRenderer] Initialisation en cours...\n";
    std::cout << "=========================================\n\n";

    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandPool();
    CreateCommandBuffer();
}

void VulkanRenderer::Shutdown() {
    std::cout << "[VulkanRenderer] Arrêt du moteur.\n";

    // Destruction du Command Pool
    if (m_CommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    }

    // Destruction des Framebuffers
    for (auto framebuffer : m_SwapChainFramebuffers) {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }

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

// ==============================================================================
// --- ÉTAPE 1 : L'INSTANCE VULKAN ---
// ==============================================================================
void VulkanRenderer::CreateInstance() {
    if (m_EnableValidationLayers && !CheckValidationLayerSupport()) {
        throw std::runtime_error("Erreur: Les Validation Layers Vulkan sont demandes, mais non disponibles !");
    }

    // 1. Informations optionnelles mais utiles pour les drivers GPU
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Cool Engine Game";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Cool Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3; // On cible une version moderne de Vulkan

    // 2. Création de l'instance proprement dite
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // 3. Récupération des extensions requises par GLFW pour pouvoir dessiner sur l'écran
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // 4. Activation des Validation Layers (Sécurité)
    if (m_EnableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
        createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
        std::cout << "[Vulkan] Validation Layers actives.\n";
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // 5. L'appel fatidique à la carte graphique !
    if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible de creer l'instance Vulkan !");
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
    // On récupère la fenêtre depuis ton architecture actuelle
    GLFWwindow* window = Application::Get().GetWindow();

    // GLFW gère la complexité de l'OS (X11, Wayland, Windows) pour nous !
    if (glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface) != VK_SUCCESS) {
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
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
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
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool; // On l'attache à notre carnet

    // PRIMARY = Ces commandes peuvent être envoyées directement au GPU pour exécution
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1; // On alloue un seul buffer pour l'instant

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Erreur fatale: Impossible d'allouer le Command Buffer !");
    }
    std::cout << "[Vulkan] Command Buffer alloue avec succes.\n";
}

// --------------------------------------------------------------

void VulkanRenderer::Clear() {
    // En Vulkan, le Clear se fait souvent au début d'une "Render Pass".
    // On laissera ça vide pour le moment.
}

void VulkanRenderer::BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
    // Préparation des buffers de commandes pour la frame
}

void VulkanRenderer::RenderScene(Scene* scene, int renderMode) {
    // Le cœur du rendu (Raytracing ou Rastérisation classique)
}

void VulkanRenderer::DrawGrid(bool enable) {
    // Rendu de la grille
}

void VulkanRenderer::EndScene() {
    // Envoi des commandes au GPU (vkQueueSubmit) et affichage à l'écran (vkQueuePresentKHR)
}

void VulkanRenderer::SetShadowResolution(uint32_t resolution) {
    // Redimensionnement des Framebuffers d'ombres
}