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
}

void VulkanRenderer::Shutdown() {
    std::cout << "[VulkanRenderer] Arrêt du moteur.\n";

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