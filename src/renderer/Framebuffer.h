#pragma once

#include <memory>
#include <cstdint>

struct FramebufferSpecification {
    uint32_t Width, Height;
    bool DepthOnly = false;
    uint32_t Layers = 1;
};

class Framebuffer {
public:
    virtual ~Framebuffer() = default;

    virtual void Bind() = 0;
    virtual void Unbind() = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual void BindDepthLayer(uint32_t layer) = 0;

    virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;
    virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;

    virtual void* GetColorAttachmentRendererID() const = 0;
    virtual void* GetDepthAttachmentRendererID() const = 0;
    virtual const FramebufferSpecification& GetSpecification() const = 0;

    // La Factory qui choisira entre OpenGL et Vulkan
    static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);
};