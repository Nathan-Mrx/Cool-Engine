#include "Framebuffer.h"

Framebuffer::Framebuffer(const FramebufferSpecification& spec)
    : m_Specification(spec) {
    Invalidate();
}

Framebuffer::~Framebuffer() {
    glDeleteFramebuffers(1, &m_RendererID);
    glDeleteTextures(1, &m_ColorAttachment);
    glDeleteRenderbuffers(1, &m_DepthAttachment);
}

void Framebuffer::Invalidate() {
    // Si on avait déjà un framebuffer (ex: lors d'un redimensionnement de la fenêtre), on le détruit
    if (m_RendererID) {
        glDeleteFramebuffers(1, &m_RendererID);
        glDeleteTextures(1, &m_ColorAttachment);
        glDeleteRenderbuffers(1, &m_DepthAttachment);
    }

    // 1. Création du FBO principal
    glGenFramebuffers(1, &m_RendererID);
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    // 2. Création de la texture de COULEUR (ce qu'on verra à l'écran)
    glGenTextures(1, &m_ColorAttachment);
    glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // On attache la texture au Framebuffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

    // 3. Création du Renderbuffer de PROFONDEUR (Depth Buffer pour la 3D)
    glGenRenderbuffers(1, &m_DepthAttachment);
    glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);
    // On attache la profondeur au Framebuffer
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthAttachment);

    // 4. Vérification de sécurité
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERREUR::FRAMEBUFFER:: Le Framebuffer n'est pas complet!" << std::endl;

    // On unbind pour ne pas dessiner dessus par erreur
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
    glViewport(0, 0, m_Specification.Width, m_Specification.Height);
}

void Framebuffer::Unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width > 8192 || height > 8192) return;
    
    m_Specification.Width = width;
    m_Specification.Height = height;
    Invalidate(); // On recrée tout avec les nouvelles dimensions
}