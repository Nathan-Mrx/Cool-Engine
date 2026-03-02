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

    void Invalidate(); // Fonction pour générer ou regénérer le FBO
    void Bind();       // On appelle ça AVANT de dessiner notre scène 3D
    void Unbind();     // On appelle ça APRÈS avoir dessiné, pour revenir à l'écran normal

    void Resize(uint32_t width, uint32_t height);

    uint32_t GetColorAttachmentRendererID() const { return m_ColorAttachment; }
    const FramebufferSpecification& GetSpecification() const { return m_Specification; }

private:
    uint32_t m_RendererID = 0;
    uint32_t m_ColorAttachment = 0;
    uint32_t m_DepthAttachment = 0; // Très important pour la 3D (Z-Buffer)
    FramebufferSpecification m_Specification;
};