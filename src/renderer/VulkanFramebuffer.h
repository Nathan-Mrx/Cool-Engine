#pragma once
#include "Framebuffer.h"
#include <vulkan/vulkan.h>

class VulkanFramebuffer : public Framebuffer {
public:
    VulkanFramebuffer(const FramebufferSpecification& spec);
    virtual ~VulkanFramebuffer();

    void Bind() override;
    void Unbind() override;
    void Resize(uint32_t width, uint32_t height) override;
    void BindDepthLayer(uint32_t layer) override {}

    int ReadPixel(uint32_t attachmentIndex, int x, int y) override { return -1; }
    void ClearAttachment(uint32_t attachmentIndex, int value) override {}

    void* GetColorAttachmentRendererID() const override { return m_ImGuiDescriptorSet; }
    void* GetDepthAttachmentRendererID() const override { return nullptr; }
    const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

    VkRenderPass GetRenderPass() const { return m_RenderPass; }
    VkFramebuffer GetVulkanFramebuffer() const { return m_Framebuffer; }

private:
    void Invalidate();
    void Release();

    // Utilitaires de création locaux
    void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

private:
    FramebufferSpecification m_Specification;

    VkImage m_ColorImage = VK_NULL_HANDLE;
    VkDeviceMemory m_ColorImageMemory = VK_NULL_HANDLE;
    VkImageView m_ColorImageView = VK_NULL_HANDLE;

    VkImage m_DepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_DepthImageMemory = VK_NULL_HANDLE;
    VkImageView m_DepthImageView = VK_NULL_HANDLE;

    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;

    VkDescriptorSet m_ImGuiDescriptorSet = VK_NULL_HANDLE;
};