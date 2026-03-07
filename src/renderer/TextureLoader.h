#pragma once
#include <glad/glad.h>
#include <stb_image.h>
#include <string>
#include <iostream>

class TextureLoader {
public:
    static unsigned int LoadTexture(const char* path) {
        unsigned int textureID;
        glGenTextures(1, &textureID);

        int width, height, nrComponents;
        stbi_set_flip_vertically_on_load(true); // Indispensable pour l'orientation OpenGL
        unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);

        if (data) {
            GLenum format = GL_RGB;
            if (nrComponents == 1) format = GL_RED;
            else if (nrComponents == 3) format = GL_RGB;
            else if (nrComponents == 4) format = GL_RGBA;

            // 1. On "sélectionne" la texture
            glBindTexture(GL_TEXTURE_2D, textureID);

            // 2. On envoie les pixels à la carte graphique
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            // 3. ON CONFIGURE LE REPEAT ICI (Tant que la texture est "bind")
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // Répète sur X (S)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); // Répète sur Y (T)

            // Lissage de la texture
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            stbi_image_free(data);
        } else {
            std::cout << "[TextureLoader] Echec du chargement : " << path << std::endl;
            stbi_image_free(data);
        }

        return textureID;
    }

    static unsigned int LoadHDR(const char* path) {
        stbi_set_flip_vertically_on_load(true);
        int width, height, nrComponents;

        // --- FIX 1 : On force 3 canaux (RGB) au lieu de 0 pour éviter les décalages mémoire ---
        float *data = stbi_loadf(path, &width, &height, &nrComponents, 3);

        unsigned int hdrTexture = 0;
        if (data) {
            glGenTextures(1, &hdrTexture);
            glBindTexture(GL_TEXTURE_2D, hdrTexture);

            // --- FIX 2 : GL_RGB32F au lieu de 16F pour encaisser la chaleur du soleil ! ---
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, data);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            stbi_image_free(data);
        } else {
            std::cout << "[TextureLoader] Echec du chargement HDR : " << path << std::endl;
        }

        return hdrTexture;
    }
};