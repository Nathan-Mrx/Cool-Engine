#pragma once
#include <glad/glad.h>
#include <stb_image.h>
#include <string>
#include <iostream>

class TextureLoader {
public:
    static uint32_t LoadTexture(const std::string& path) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(false);

        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

        if (!data) {
            std::cerr << "[TextureLoader] Failed to load: " << path << std::endl;
            return 0;
        }

        uint32_t textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Paramètres de filtrage pour un rendu propre sur ton GPU Nvidia
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);
        return textureID;
    }
};