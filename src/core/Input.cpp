#include "Input.h"

GLFWwindow* Input::s_Window = nullptr;

void Input::Init(GLFWwindow* window) {
    s_Window = window;
}

bool Input::IsKeyPressed(int keycode) {
    auto state = glfwGetKey(s_Window, keycode);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

bool Input::IsMouseButtonPressed(int button) {
    auto state = glfwGetMouseButton(s_Window, button);
    return state == GLFW_PRESS;
}

glm::vec2 Input::GetMousePosition() {
    double xpos, ypos;
    glfwGetCursorPos(s_Window, &xpos, &ypos);
    return { (float)xpos, (float)ypos };
}