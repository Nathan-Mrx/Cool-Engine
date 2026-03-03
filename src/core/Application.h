#pragma once
#include <string>
#include <memory>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class EditorLayer;

class Application {
public:
    Application(const std::string& name = "Cool Engine", int width = 1600, int height = 900);
    ~Application();

    void Run();
    void SetWindowIcon(const std::string& path);
    void Close() { m_Running = false; }

    static Application& Get() { return *s_Instance; }
    GLFWwindow* GetWindow() const { return m_Window; }

private:
    void Init();
    void Shutdown();

private:
    GLFWwindow* m_Window;
    bool m_Running = true;
    float m_DeltaTime = 0.0f;
    float m_LastFrameTime = 0.0f;

    std::unique_ptr<EditorLayer> m_EditorLayer;

    static Application* s_Instance;
};