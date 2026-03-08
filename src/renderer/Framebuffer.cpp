#include "Framebuffer.h"
#include "RendererAPI.h"
#include "OpenGLFramebuffer.h"
#include "VulkanFramebuffer.h"
#include <stdexcept>

std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) {
    switch (RendererAPI::GetAPI()) {
    case RendererAPI::API::OpenGL:
        return std::make_shared<OpenGLFramebuffer>(spec);
    case RendererAPI::API::Vulkan:
        return std::make_shared<VulkanFramebuffer>(spec);
    case RendererAPI::API::None:
        throw std::runtime_error("RendererAPI::None n'est pas supporte pour le Framebuffer !");
    }
    return nullptr;
}