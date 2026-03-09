#pragma once

#include "RendererAPI.h"
#include "VulkanRenderer.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <imgui_impl_vulkan.h>
#include <string>
#include <iostream>

class TextureLoader {
public:
    static void* LoadTexture(const char* path) {
        int width, height, nrComponents;

        // =======================================================
        // --- VULKAN : L'ENVOI MANUEL EN VRAM ---
        // =======================================================
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {

            // Vulkan lit les images à l'endroit (contrairement à OpenGL)
            stbi_set_flip_vertically_on_load(false);

            // On force 4 canaux (RGBA) car le format RGB (3 canaux) n'est pas
            // garanti d'être supporté par toutes les cartes graphiques en Vulkan.
            unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 4);

            if (!data) {
                std::cout << "[TextureLoader] Echec du chargement Vulkan : " << path << std::endl;
                return nullptr;
            }

            VulkanRenderer* vkRenderer = VulkanRenderer::Get();
            VkDevice device = vkRenderer->GetDevice();

            // 1. CRÉATION DU SAS (Staging Buffer CPU)
            VkDeviceSize imageSize = width * height * 4;
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingBufferMemory;
            vkRenderer->CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     stagingBuffer, stagingBufferMemory);

            // 2. COPIE DES PIXELS DANS LE SAS
            void* mappedData;
            vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &mappedData);
            memcpy(mappedData, data, static_cast<size_t>(imageSize));
            vkUnmapMemory(device, stagingBufferMemory);
            stbi_image_free(data);

            // 3. CRÉATION DE L'IMAGE DANS LA VRAM (VkImage)
            VkImage textureImage;
            VkDeviceMemory textureImageMemory;

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // Format de couleur standard
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            vkCreateImage(device, &imageInfo, nullptr, &textureImage);

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, textureImage, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = vkRenderer->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            vkAllocateMemory(device, &allocInfo, nullptr, &textureImageMemory);
            vkBindImageMemory(device, textureImage, textureImageMemory, 0);

            // 4. ORDRES AU GPU : TRANSFERT ET CHANGEMENT DE LAYOUT
            // On prépare l'image à recevoir les données
            vkRenderer->TransitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            // On copie depuis le sas
            vkRenderer->CopyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            // On prépare l'image à être lue par les Shaders (ImGui)
            vkRenderer->TransitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            // On détruit le sas qui ne sert plus à rien !
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);

            // 5. CRÉATION DES "LUNETTES" POUR LIRE L'IMAGE (ImageView)
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = textureImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView textureImageView;
            vkCreateImageView(device, &viewInfo, nullptr, &textureImageView);

            // 6. CRÉATION DE LA FAÇON DE DESSINER L'IMAGE (Sampler)
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

            VkSampler textureSampler;
            vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler);

            // 7. LE PONT MAGIQUE AVEC IMGUI ET LE MOTEUR 3D !
            VulkanTexture* tex = new VulkanTexture();
            tex->Image = textureImage;
            tex->Memory = textureImageMemory;
            tex->View = textureImageView;
            tex->Sampler = textureSampler;
            tex->ImGuiDescriptor = (void*)ImGui_ImplVulkan_AddTexture(textureSampler, textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            // On le déguise en void* pour respecter l'architecture ECS de base !
            return (void*)tex;
        }

        // =======================================================
        // --- OPENGL : L'ANCIENNE MÉTHODE ---
        // =======================================================
        else if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            unsigned int textureID = 0;
            glGenTextures(1, &textureID);

            stbi_set_flip_vertically_on_load(true); // OpenGL aime bien les images inversées
            unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);

            if (data) {
                GLenum format = GL_RGB;
                if (nrComponents == 1) format = GL_RED;
                else if (nrComponents == 3) format = GL_RGB;
                else if (nrComponents == 4) format = GL_RGBA;

                glBindTexture(GL_TEXTURE_2D, textureID);
                glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                glGenerateMipmap(GL_TEXTURE_2D);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                stbi_image_free(data);
            } else {
                std::cout << "[TextureLoader] Echec du chargement OpenGL : " << path << std::endl;
                stbi_image_free(data);
            }

            return (void*)(uintptr_t)textureID;
        }

        return nullptr;
    }

    static void* LoadHDR(const char* path) {
        // =======================================================
        // --- VULKAN : CHARGEMENT D'IMAGE FLOTTANTE (HDR) ---
        // =======================================================
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            stbi_set_flip_vertically_on_load(true);
            int width, height, nrComponents;

            // On force 4 canaux (RGBA) en FLOAT pour Vulkan !
            float* data = stbi_loadf(path, &width, &height, &nrComponents, 4);

            if (!data) {
                std::cout << "[TextureLoader] Echec du chargement HDR Vulkan : " << path << std::endl;
                return nullptr;
            }

            VulkanRenderer* vkRenderer = VulkanRenderer::Get();
            VkDevice device = vkRenderer->GetDevice();

            // 1. CRÉATION DU SAS (4 floats par pixel, soit 16 octets/pixel !)
            VkDeviceSize imageSize = width * height * 4 * sizeof(float);
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingBufferMemory;
            vkRenderer->CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

            void* mappedData;
            vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &mappedData);
            memcpy(mappedData, data, static_cast<size_t>(imageSize));
            vkUnmapMemory(device, stagingBufferMemory);

            stbi_image_free(data); // On libère la RAM CPU

            // 2. CRÉATION DE L'IMAGE VULKAN (Format R32G32B32A32_SFLOAT)
            VulkanTexture* tex = new VulkanTexture();

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT; // <--- LE FORMAT MAGIQUE HDR
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            vkCreateImage(device, &imageInfo, nullptr, &tex->Image);

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, tex->Image, &memRequirements);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = vkRenderer->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            vkAllocateMemory(device, &allocInfo, nullptr, &tex->Memory);
            vkBindImageMemory(device, tex->Image, tex->Memory, 0);

            // 3. TRANSFERT ET TRANSITIONS
            vkRenderer->TransitionImageLayout(tex->Image, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkRenderer->CopyBufferToImage(stagingBuffer, tex->Image, width, height);
            vkRenderer->TransitionImageLayout(tex->Image, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);

            // 4. IMAGE VIEW ET SAMPLER
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = tex->Image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &viewInfo, nullptr, &tex->View);

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            vkCreateSampler(device, &samplerInfo, nullptr, &tex->Sampler);

            // 5. PONT IMGUI POUR LE CONTENT BROWSER
            tex->ImGuiDescriptor = (void*)ImGui_ImplVulkan_AddTexture(tex->Sampler, tex->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            return (void*)tex;
        }

        // =======================================================
        // --- OPENGL : L'ANCIENNE MÉTHODE ---
        // =======================================================
        stbi_set_flip_vertically_on_load(true);
        int width, height, nrComponents;
        float *data = stbi_loadf(path, &width, &height, &nrComponents, 3);

        unsigned int hdrTexture = 0;
        if (data) {
            glGenTextures(1, &hdrTexture);
            glBindTexture(GL_TEXTURE_2D, hdrTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, data);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            stbi_image_free(data);
        } else {
            std::cout << "[TextureLoader] Echec du chargement HDR OpenGL : " << path << std::endl;
        }
        return (void*)(uintptr_t)hdrTexture;
    }

    // --- NOUVEAU : DÉCODEUR POUR IMGUI ---
    static void* GetImGuiTextureID(void* texturePtr) {
        if (!texturePtr) return nullptr;

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // En Vulkan, on déballe la structure pour donner le vrai pointeur ImGui
            return static_cast<VulkanTexture*>(texturePtr)->ImGuiDescriptor;
        }

        // En OpenGL, le pointeur est DÉJÀ l'ID de la texture
        return texturePtr;
    }
};

