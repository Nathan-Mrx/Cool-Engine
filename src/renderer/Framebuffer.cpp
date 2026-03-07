#include "Framebuffer.h"

Framebuffer::Framebuffer(const FramebufferSpecification& spec)
    : m_Specification(spec) {
    Invalidate();
}

Framebuffer::~Framebuffer() {
    glDeleteFramebuffers(1, &m_RendererID);
    glDeleteTextures(1, &m_ColorAttachment);
    glDeleteTextures(1, &m_EntityIDAttachment);

    // Nettoyage adapté au type de profondeur
    if (m_Specification.DepthOnly) {
        glDeleteTextures(1, &m_DepthAttachment);
    } else {
        glDeleteRenderbuffers(1, &m_DepthAttachment);
    }
}

void Framebuffer::Invalidate() {
    if (m_RendererID) {
        glDeleteFramebuffers(1, &m_RendererID);
        glDeleteTextures(1, &m_ColorAttachment);
        glDeleteTextures(1, &m_EntityIDAttachment);
        if (m_Specification.DepthOnly) {
            glDeleteTextures(1, &m_DepthAttachment);
        } else {
            glDeleteRenderbuffers(1, &m_DepthAttachment);
        }
    }

    glGenFramebuffers(1, &m_RendererID);
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    // --- LE BRANCHEMENT PROPRE ---
    if (m_Specification.DepthOnly) {
        CreateShadowMap();
    } else {
        CreateNormal();
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERREUR::FRAMEBUFFER:: Le Framebuffer n'est pas complet!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::CreateShadowMap() {
    glGenTextures(1, &m_DepthAttachment);
    glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_Specification.Width, m_Specification.Height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    // Paramètres pour éviter les artefacts sur les bords des ombres
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);

    // On dit explicitement à OpenGL qu'on ne dessinera aucune couleur !
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

void Framebuffer::CreateNormal() {
    // 1. Texture de COULEUR (Attachment 0)
    glGenTextures(1, &m_ColorAttachment);
    glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

    // 2. Texture des IDs (Attachment 1)
    glGenTextures(1, &m_EntityIDAttachment);
    glBindTexture(GL_TEXTURE_2D, m_EntityIDAttachment);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, m_Specification.Width, m_Specification.Height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_EntityIDAttachment, 0);

    // 3. Profondeur classique (Renderbuffer)
    glGenRenderbuffers(1, &m_DepthAttachment);
    glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthAttachment);

    // 4. On dit à OpenGL qu'on dessine dans DEUX textures en même temps
    GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, buffers);
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
    Invalidate();
}

int Framebuffer::ReadPixel(uint32_t attachmentIndex, int x, int y) {
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
    int pixelData;
    glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
    return pixelData;
}

void Framebuffer::ClearAttachment(uint32_t attachmentIndex, int value) {
    glClearBufferiv(GL_COLOR, attachmentIndex, &value);
}