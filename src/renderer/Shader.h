#pragma once
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader {
public:
    unsigned int ID; // L'identifiant OpenGL du programme

    Shader(const char* vertexPath, const char* fragmentPath);
    Shader(const char* computePath);

    ~Shader();

    void Use();
    
    // Utilitaires pour envoyer des données au shader (Uniforms)
    void SetBool(const std::string& name, bool value) const;
    void SetInt(const std::string& name, int value) const;
    void SetInt3(const std::string& name, int v0, int v1, int v2) const;
    void SetFloat(const std::string& name, float value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;
    void SetVec4(const std::string& name, const glm::vec4& value) const;
    void SetMat4(const std::string& name, const glm::mat4& mat) const;

    // --- NOUVELLES MÉTHODES COMPUTE ---
    // Lance l'exécution des threads sur le GPU
    void Dispatch(unsigned int numGroupsX, unsigned int numGroupsY, unsigned int numGroupsZ);
    // Force le GPU à attendre que les calculs soient finis avant de lire les résultats
    static void IssueMemoryBarrier(unsigned int flags);

private:
    void CheckCompileErrors(unsigned int shader, std::string type);
};