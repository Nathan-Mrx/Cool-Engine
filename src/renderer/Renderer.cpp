#include "Renderer.h"
#include "../ecs/Components.h"
#include <glad/glad.h>

Renderer::RendererData* Renderer::s_Data = new Renderer::RendererData();

void Renderer::Init() {
    s_Data->MainShader = std::make_unique<Shader>("shaders/default.vert", "shaders/default.frag");
    s_Data->GridShader = std::make_unique<Shader>("shaders/grid.vert", "shaders/grid.frag");
    s_Data->LineShader = std::make_unique<Shader>("shaders/line.vert", "shaders/line.frag");
    s_Data->OutlineShader = std::make_unique<Shader>("shaders/outline.vert", "shaders/outline.frag");

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

    // --- INITIALISATION DE LA BOÎTE DE DEBUG ---
    float boxVertices[] = {
        // Les 12 lignes qui forment un cube (de -1 à 1)
        -1, -1, -1,   1, -1, -1,    1, -1, -1,   1, -1,  1,
         1, -1,  1,  -1, -1,  1,   -1, -1,  1,  -1, -1, -1,
        -1,  1, -1,   1,  1, -1,    1,  1, -1,   1,  1,  1,
         1,  1,  1,  -1,  1,  1,   -1,  1,  1,  -1,  1, -1,
        -1, -1, -1,  -1,  1, -1,    1, -1, -1,   1,  1, -1,
         1, -1,  1,   1,  1,  1,   -1, -1,  1,  -1,  1,  1
    };

    glGenVertexArrays(1, &s_Data->DebugBoxVAO);
    glGenBuffers(1, &s_Data->DebugBoxVBO);
    glBindVertexArray(s_Data->DebugBoxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, s_Data->DebugBoxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(boxVertices), boxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
}


void Renderer::RenderScene(Scene* scene, int renderMode) {
    if (s_Data->MainShader) {
        s_Data->MainShader->Use();
        s_Data->MainShader->SetInt("uRenderMode", renderMode); // On prévient le shader !
    }

    // Si on est en mode Wireframe (2), on demande à OpenGL de ne dessiner que les lignes
    if (renderMode == 2) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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

            // --- NOUVEAU : On envoie l'identifiant numérique de l'entité au Shader ---
            s_Data->MainShader->SetInt("uEntityID", (int)entity);

            mesh.MeshData->Draw();
        }
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    // AJOUT DU GL_STENCIL_BUFFER_BIT ICI :
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Renderer::DrawDebugArrow(const glm::vec3& start, const glm::vec3& forward, const glm::vec3& right, const glm::vec3& up, const glm::vec3& color, const glm::mat4& view, const glm::mat4& projection, float length) {

    float headSize = length * 0.25f;

    glm::vec3 end = start + forward * length;

    glm::vec3 p1 = end - forward * headSize + right * headSize;
    glm::vec3 p2 = end - forward * headSize - right * headSize;
    glm::vec3 p3 = end - forward * headSize + up * headSize;
    glm::vec3 p4 = end - forward * headSize - up * headSize;

    float vertices[] = {
        start.x, start.y, start.z,   end.x, end.y, end.z,
        end.x, end.y, end.z,         p1.x, p1.y, p1.z,
        end.x, end.y, end.z,         p2.x, p2.y, p2.z,
        end.x, end.y, end.z,         p3.x, p3.y, p3.z,
        end.x, end.y, end.z,         p4.x, p4.y, p4.z
    };

    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // --- LE FIX : UTILISATION DU BON SHADER ET DES MATRICES ---
    if (s_Data->LineShader) {
        s_Data->LineShader->Use();
        s_Data->LineShader->SetMat4("uModel", glm::mat4(1.0f));

        // Attention à bien utiliser les noms que tu mettras dans line.vert !
        // J'utilise "uView" et "uProjection" car c'est ta convention dans MainShader.
        s_Data->LineShader->SetMat4("uView", view);
        s_Data->LineShader->SetMat4("uProjection", projection);
        s_Data->LineShader->SetVec3("uColor", color);
    }

    glDrawArrays(GL_LINES, 0, 10);

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
}

void Renderer::BeginOutlineMask(const glm::mat4& transform) {
    if (!s_Data->MainShader) return;    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilMask(0xFF);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    // --- NOUVEAU : Fix de l'Image 5 ---
    // On désactive la profondeur pour que le masque englobe
    // TOUT l'objet, même s'il est caché derrière un mur ou le sol.
    glDisable(GL_DEPTH_TEST);

    s_Data->MainShader->Use();
    s_Data->MainShader->SetMat4("uModel", transform);
}

void Renderer::BeginOutlineDraw(const glm::mat4& transform, const glm::vec3& color) {
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilMask(0x00);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // --- NOUVEAU : Fix de l'Image 5 (Suite) ---
    // On garde la profondeur désactivée pour que l'outline se
    // dessine PAR-DESSUS tout le reste du monde.
    glDisable(GL_DEPTH_TEST);

    // --- NOUVEAU : Fix de l'Image 3 et 4 ---
    // L'astuce du fil de fer !
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(6.0f); // Épaisseur de ton outline. Ajuste selon tes goûts !

    // On utilise le shader standard, plus besoin de gonfler l'objet
    s_Data->MainShader->Use();
    s_Data->MainShader->SetVec3("uColor", color);
    s_Data->MainShader->SetMat4("uModel", transform);
    s_Data->MainShader->SetFloat("uAmbientStrength", 1.0f);
    s_Data->MainShader->SetFloat("uDiffuseStrength", 0.0f);
}

void Renderer::EndOutline() {
    // Nettoyage absolu pour ne pas corrompre la frame suivante
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);

    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glDisable(GL_STENCIL_TEST);

    // On réactive le Z-Buffer pour le rendu normal !
    glEnable(GL_DEPTH_TEST);
}

void Renderer::DrawDebugBox(const glm::mat4& transform, const glm::vec3& color) {
    if (!s_Data->LineShader) return;

    s_Data->LineShader->Use();
    s_Data->LineShader->SetMat4("uModel", transform);
    s_Data->LineShader->SetMat4("uView", s_Data->CurrentView);
    s_Data->LineShader->SetMat4("uProjection", s_Data->CurrentProjection);
    s_Data->LineShader->SetVec3("uColor", color);

    glBindVertexArray(s_Data->DebugBoxVAO);
    glDrawArrays(GL_LINES, 0, 24); // 12 lignes * 2 sommets = 24
}