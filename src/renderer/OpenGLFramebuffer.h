#pragma once
#include "Framebuffer.h"
#include <glad/glad.h>

class OpenGLFramebuffer : public Framebuffer {
public:
    OpenGLFramebuffer(const FramebufferSpecification& spec);
    virtual ~OpenGLFramebuffer();

    void Invalidate();
    void Bind() override;
    void Unbind() override;
    void Resize(uint32_t width, uint32_t height) override;
    void BindDepthLayer(uint32_t layer) override;

    int ReadPixel(uint32_t attachmentIndex, int x, int y) override;
    void ClearAttachment(uint32_t attachmentIndex, int value) override;

    uint32_t GetColorAttachmentRendererID() const override { return m_ColorAttachment; }
    uint32_t GetDepthAttachmentRendererID() const override { return m_DepthAttachment; }
    const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

private:
    void CreateShadowMap();
    void CreateNormal();

private:
    uint32_t m_RendererID = 0;
    uint32_t m_ColorAttachment = 0;
    uint32_t m_EntityIDAttachment = 0;
    uint32_t m_DepthAttachment = 0;
    FramebufferSpecification m_Specification;
};