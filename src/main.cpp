#include "core/Application.h"
#include "project/Project.h"
#include <filesystem>
#include <iostream>

#include <glad/glad.h> // Indispensable pour charger les fonctions GL
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "renderer/Renderer.h"

void ShowSplashScreen(GLFWwindow*& splashWindow) {
    // 1. On force un profil de compatibilité pour utiliser glBegin/glEnd (plus simple pour un splash)
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

    splashWindow = glfwCreateWindow(544, 864, "Cool Engine Loading...", NULL, NULL);
    if (!splashWindow) return;

    glfwMakeContextCurrent(splashWindow);

    // --- CRUCIAL : ON INITIALISE GLAD POUR LE SPLASH ---
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD for Splash Screen" << std::endl;
        return;
    }

    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load("splash.png", &width, &height, &channels, 4);

    if (data) {
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);

        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(-1, -1);
            glTexCoord2f(1, 0); glVertex2f(1, -1);
            glTexCoord2f(1, 1); glVertex2f(1, 1);
            glTexCoord2f(0, 1); glVertex2f(-1, 1);
        glEnd();

        glfwSwapBuffers(splashWindow);
    }
}

int main(int argc, char** argv) {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        std::filesystem::current_path(std::filesystem::path(result).parent_path());
    }

    if (!glfwInit()) return -1;

    GLFWwindow* splashWindow = nullptr;
    ShowSplashScreen(splashWindow);

    // --- RÉINITIALISATION POUR LA FENÊTRE PRINCIPALE ---
    glfwDefaultWindowHints();

    // 1. On crée d'abord l'application (et sa fenêtre principale)
    Application app("Cool Engine", 1280, 720);

    // 2. ON REND LE CONTEXTE DE L'APP ACTUEL AVANT D'INITIALISER LE RENDERER
    glfwMakeContextCurrent(app.GetWindow());

    // On recharge GLAD pour le nouveau contexte de la fenêtre principale
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // 3. Maintenant on initialise le Renderer : les ressources vivront
    // dans le contexte de l'application et ne seront pas détruites !
    Renderer::Init();

    if (argc > 1) {
        std::filesystem::path projectPath = argv[1];
        if (std::filesystem::exists(projectPath) && projectPath.extension() == ".ceproj") {
            Project::Load(projectPath.string());
        }
    }

    // Une fois que tout est prêt, on ferme le splash
    glfwDestroyWindow(splashWindow);

    app.SetWindowIcon("icon.png");
    app.Run();

    return 0;
}