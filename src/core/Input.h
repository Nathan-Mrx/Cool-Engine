#pragma once
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

class Input {
public:
    // Initialise le système d'input avec la fenêtre principale
    static void Init(GLFWwindow* window);

    // Vérifie si une touche est actuellement pressée
    static bool IsKeyPressed(int keycode);
    static bool IsMouseButtonPressed(int button);
    static glm::vec2 GetMousePosition();

private:
    static GLFWwindow* s_Window;
};
