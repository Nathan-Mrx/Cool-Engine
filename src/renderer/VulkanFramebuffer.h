#pragma once
#include "Framebuffer.h"
#include <iostream>

class VulkanFramebuffer : public Framebuffer {
public:
    VulkanFramebuffer(const FramebufferSpecification& spec) : m_Specification(spec) {
        std::cout << "[Vulkan] Creation d'un Framebuffer Offscreen (Stub)\\n";
    }
    virtual ~VulkanFramebuffer() = default;

    void Bind() override {}
    void Unbind() override {}
    void Resize(uint32_t width, uint32_t height) override { m_Specification.Width = width; m_Specification.Height = height; }
    void BindDepthLayer(uint32_t layer) override {}

    int ReadPixel(uint32_t attachmentIndex, int x, int y) override { return -1; }
    void ClearAttachment(uint32_t attachmentIndex, int value) override {}

    uint32_t GetColorAttachmentRendererID() const override { return 0; } // ImGui recevra 0 pour l'instant
    uint32_t GetDepthAttachmentRendererID() const override { return 0; }
    const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

private:
    FramebufferSpecification m_Specification;
};