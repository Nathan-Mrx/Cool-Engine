#include "Renderer.h"
#include "../ecs/Components.h"
#include <glad/glad.h>

Renderer::RendererData* Renderer::s_Data = new Renderer::RendererData();

void Renderer::Init() {
    s_Data->MainShader = std::make_unique<Shader>("shaders/default.vert", "shaders/default.frag");
    s_Data->GridShader = std::make_unique<Shader>("shaders/grid.vert", "shaders/grid.frag");

    // --- INITIALISATION DE LA GRILLE ---
    float gridVertices[] = {
        // Coordonnées (X, Y, Z) pour un quad immense couvrant le sol
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f
    };

    glGenVertexArrays(1, &s_Data->GridVAO);
    glGenBuffers(1, &s_Data->GridVBO);

    glBindVertexArray(s_Data->GridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, s_Data->GridVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(gridVertices), gridVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
}


void Renderer::RenderScene(Scene* scene) {
    if (s_Data->MainShader) {
        s_Data->MainShader->Use();
    }

    // --- 1. GESTION DE LA LUMIÈRE ---
    auto lightView = scene->m_Registry.view<TransformComponent, DirectionalLightComponent>();
    bool hasLight = false;

    // Itération standard EnTT (Supporte les vues multiples)
    for (auto entity : lightView) {
        auto [lightTransform, light] = lightView.get<TransformComponent, DirectionalLightComponent>(entity);

        // --- LE FIX : Calcul de la direction avec le Quaternion ---
        // On prend le vecteur de base de la lumière (vers le bas) et on le tourne avec le Quat.
        // C'est beaucoup plus performant qu'une multiplication de matrices 4x4 !
        glm::vec3 baseDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 lightDir = glm::normalize(lightTransform.Rotation * baseDirection);

        s_Data->MainShader->SetVec3("uLightColor", light.Color);
        s_Data->MainShader->SetVec3("uLightDir", lightDir);
        s_Data->MainShader->SetFloat("uAmbientStrength", light.AmbientIntensity);
        s_Data->MainShader->SetFloat("uDiffuseStrength", light.DiffuseIntensity);

        hasLight = true;
        break; // On ne gère qu'une seule Directional Light globale pour l'instant
    }

    if (!hasLight) {
        // Fallback sans lumière : on éclaire tout
        s_Data->MainShader->SetVec3("uLightColor", glm::vec3(1.0f));
        s_Data->MainShader->SetVec3("uLightDir", glm::vec3(0.0f, -1.0f, 0.0f));
        s_Data->MainShader->SetFloat("uAmbientStrength", 1.0f);
        s_Data->MainShader->SetFloat("uDiffuseStrength", 0.0f);
    }

    // --- 2. RENDU DES MESHES ---
    auto view = scene->m_Registry.view<TransformComponent, MeshComponent, ColorComponent>();
    for (auto entity : view) {
        auto [transform, mesh, color] = view.get<TransformComponent, MeshComponent, ColorComponent>(entity);
        if (mesh.MeshData) {
            s_Data->MainShader->SetVec3("uColor", color.Color);
            s_Data->MainShader->SetMat4("uModel", transform.GetTransform());
            mesh.MeshData->Draw();
        }
    }
}

void Renderer::Shutdown() {
    // Nettoyage des shaders et du pointeur s_Data
    s_Data->MainShader.reset();
    s_Data->GridShader.reset();
    glDeleteVertexArrays(1, &s_Data->GridVAO);
    glDeleteBuffers(1, &s_Data->GridVBO);

    delete s_Data;
}

void Renderer::BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos) {
    // 1. Sauvegarde des matrices pour les autres shaders (comme la grille)
    s_Data->CurrentView = view;
    s_Data->CurrentProjection = projection;

    // 2. Activer le shader principal
    s_Data->MainShader->Use();

    // 3. Envoyer les matrices au shader principal
    s_Data->MainShader->SetMat4("uView", view);
    s_Data->MainShader->SetMat4("uProjection", projection);

    // 4. Envoyer la position de la caméra
    s_Data->MainShader->SetVec3("uCameraPos", camPos);

    // 5. Configuration de l'état OpenGL
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::EndScene() {
    // Ici, tu pourrais ajouter des effets de post-process
    // ou simplement nettoyer l'état si nécessaire.
}

void Renderer::DrawGrid(bool show) {
    if (!show || !s_Data->GridShader) return;

    // ... (glEnable, etc.)
    s_Data->GridShader->Use();

    // IL FAUT RENVOYER LES MATRICES ICI AUSSI
    // On peut les stocker dans s_Data lors du BeginScene pour les réutiliser
    s_Data->GridShader->SetMat4("uView", s_Data->CurrentView);
    s_Data->GridShader->SetMat4("uProjection", s_Data->CurrentProjection);

    glBindVertexArray(s_Data->GridVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // ...
}

void Renderer::Clear() {
    // Un gris neutre très "Engine" (0.1, 0.1, 0.1)
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}