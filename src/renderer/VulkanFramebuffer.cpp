#include "VulkanFramebuffer.h"
#include "VulkanRenderer.h"
#include <imgui_impl_vulkan.h>
#include <stdexcept>

VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec) : m_Specification(spec) {
    Invalidate();
}

VulkanFramebuffer::~VulkanFramebuffer() {
    Release();
}

void VulkanFramebuffer::Release() {
    VkDevice device = VulkanRenderer::Get()->GetDevice();
    if (!device) return;

    vkDeviceWaitIdle(device); // GPU en pause avant suppression !

    if (m_ImGuiDescriptorSet) {
        ImGui_ImplVulkan_RemoveTexture(m_ImGuiDescriptorSet);
        m_ImGuiDescriptorSet = VK_NULL_HANDLE;
    }
    if (m_Sampler) vkDestroySampler(device, m_Sampler, nullptr);
    if (m_Framebuffer) vkDestroyFramebuffer(device, m_Framebuffer, nullptr);

    if (m_ColorImageView) vkDestroyImageView(device, m_ColorImageView, nullptr);
    if (m_ColorImage) vkDestroyImage(device, m_ColorImage, nullptr);
    if (m_ColorImageMemory) vkFreeMemory(device, m_ColorImageMemory, nullptr);

    if (m_DepthImageView) vkDestroyImageView(device, m_DepthImageView, nullptr);
    if (m_DepthImage) vkDestroyImage(device, m_DepthImage, nullptr);
    if (m_DepthImageMemory) vkFreeMemory(device, m_DepthImageMemory, nullptr);
}

void VulkanFramebuffer::Invalidate() {
    if (m_Specification.Width == 0 || m_Specification.Height == 0) return;
    Release();
    
    VkDevice device = VulkanRenderer::Get()->GetDevice();
    
    // 1. CREATION MÉMOIRE (Couleur + Profondeur)
    CreateImage(m_Specification.Width, m_Specification.Height, VK_FORMAT_R8G8B8A8_UNORM, 
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_ColorImage, m_ColorImageMemory);
    m_ColorImageView = CreateImageView(m_ColorImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    CreateImage(m_Specification.Width, m_Specification.Height, VK_FORMAT_D32_SFLOAT, 
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_DepthImage, m_DepthImageMemory);
    m_DepthImageView = CreateImageView(m_DepthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

    // 2. RENDER PASS HORS ÉCRAN
    // On utilise le RenderPass officiel du moteur pour garantir la compatibilité !
    m_RenderPass = VulkanRenderer::Get()->GetSceneRenderPass();

    // 3. FRAMEBUFFER
    std::array<VkImageView, 2> fbAttachments = {m_ColorImageView, m_DepthImageView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_RenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
    framebufferInfo.pAttachments = fbAttachments.data();
    framebufferInfo.width = m_Specification.Width;
    framebufferInfo.height = m_Specification.Height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_Framebuffer) != VK_SUCCESS) throw std::runtime_error("Echec Framebuffer");

    // 4. SAMPLER POUR LECTURE
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) throw std::runtime_error("Echec Sampler");

    // 5. PONT MAGIQUE AVEC IMGUI !
    m_ImGuiDescriptorSet = ImGui_ImplVulkan_AddTexture(m_Sampler, m_ColorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanFramebuffer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (m_Specification.Width == width && m_Specification.Height == height)) return;
    m_Specification.Width = width;
    m_Specification.Height = height;
    Invalidate();
}

void VulkanFramebuffer::Bind() {
    VulkanRenderer::Get()->SetTargetFramebuffer(this); // On indique la cible !
}

void VulkanFramebuffer::Unbind() {
    VulkanRenderer::Get()->SetTargetFramebuffer(nullptr); // On relâche
}

// --- ALLOCATEURS ---
void VulkanFramebuffer::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkDevice device = VulkanRenderer::Get()->GetDevice();
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
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) throw std::runtime_error("Echec allocation image");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = VulkanRenderer::Get()->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) throw std::runtime_error("Echec VRAM");
    vkBindImageMemory(device, image, memory, 0);
}

VkImageView VulkanFramebuffer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(VulkanRenderer::Get()->GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) throw std::runtime_error("Echec ImageView");
    return imageView;
}