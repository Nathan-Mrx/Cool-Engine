#pragma once

#include <glad/glad.h>
#include <iostream>

struct FramebufferSpecification {
    uint32_t Width, Height;
    // On pourrait ajouter d'autres options ici plus tard (MSAA, formats spécifiques...)
};

class Framebuffer {
public:
    Framebuffer(const FramebufferSpecification& spec);
    ~Framebuffer();

    void Invalidate();
    void Bind();
    void Unbind();

    void Resize(uint32_t width, uint32_t height);

    uint32_t GetColorAttachmentRendererID() const { return m_ColorAttachment; }
    const FramebufferSpecification& GetSpecification() const { return m_Specification; }

    int ReadPixel(uint32_t attachmentIndex, int x, int y);
    void ClearAttachment(uint32_t attachmentIndex, int value);

private:
    uint32_t m_RendererID = 0;
    uint32_t m_ColorAttachment = 0;
    uint32_t m_EntityIDAttachment = 0; // <-- NOUVEAU : Texture pour les IDs
    uint32_t m_DepthAttachment = 0;
    FramebufferSpecification m_Specification;
};