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
};