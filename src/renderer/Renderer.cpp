#include "Renderer.h"
#include "../ecs/Components.h"
#include <glad/glad.h>

Renderer::RendererData* Renderer::s_Data = new Renderer::RendererData();

void Renderer::Init() {
    s_Data->MainShader = std::make_unique<Shader>("shaders/default.vert", "shaders/default.frag");
    s_Data->GridShader = std::make_unique<Shader>("shaders/grid.vert", "shaders/grid.frag");
    s_Data->LineShader = std::make_unique<Shader>("shaders/line.vert", "shaders/line.frag");
    s_Data->OutlineShader = std::make_unique<Shader>("shaders/outline.vert", "shaders/outline.frag");

    // shadow
    s_Data->ShadowShader = std::make_unique<Shader>("shaders/shadow.vert", "shaders/shadow.frag");

    FramebufferSpecification shadowSpec;
    shadowSpec.Width = 2048;  // Haute résolution (2K) pour des ombres nettes
    shadowSpec.Height = 2048;
    shadowSpec.DepthOnly = true; // Mode spécial qu'on a codé juste avant !
    s_Data->ShadowFramebuffer = std::make_unique<Framebuffer>(shadowSpec);

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
    // =====================================================
    // 1. LECTURE GLOBALE DE LA LUMIÈRE
    // =====================================================
    auto lightView = scene->m_Registry.view<TransformComponent, DirectionalLightComponent>();

    glm::vec3 currentLightColor = glm::vec3(1.0f);
    glm::vec3 currentLightDir = glm::vec3(0.0f, 0.0f, -1.0f); // <-- FIX : Vers le bas en Z-up
    float currentAmbient = 1.0f;
    float currentDiffuse = 0.0f;

    for (auto entity : lightView) {
        auto [lightTransform, light] = lightView.get<TransformComponent, DirectionalLightComponent>(entity);

        // --- FIX : La direction de base est -Z ---
        glm::vec3 baseDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        currentLightDir = glm::normalize(lightTransform.Rotation * baseDirection);

        currentLightColor = light.Color;
        currentAmbient = light.AmbientIntensity;
        currentDiffuse = light.DiffuseIntensity;
        break;
    }

    // On récupère la position ET la direction de la caméra depuis la matrice
    glm::mat4 invView = glm::inverse(s_Data->CurrentView);
    glm::vec3 camPos = glm::vec3(invView[3]);

    // Dans une matrice de vue OpenGL, la 3ème colonne correspond au vecteur "Backward" (Arrière).
    // Donc l'inverse de ça, c'est notre vecteur "Forward" (Avant) !
    glm::vec3 camFront = -glm::normalize(glm::vec3(invView[2]));

    // 1. On crée un "Point Cible" à 15 mètres (1500 cm) DEVANT la caméra
    glm::vec3 targetPos = camPos + camFront * 1500.0f;

    // 2. On réduit la boîte d'ombre à 30 mètres de large (1500 cm de chaque côté du centre)
    // On réduit aussi le near/far pour optimiser la précision du Z-Buffer (le float en 32 bits)
    glm::mat4 lightProjection = glm::ortho(-1500.0f, 1500.0f, -1500.0f, 1500.0f, 10.0f, 10000.0f);

    // 3. On recule le soleil virtuel de 50 mètres (5000 cm) par rapport à la CIBLE (et non la caméra)
    glm::vec3 lightPos = targetPos - (currentLightDir * 5000.0f);

    // --- FIX Z-UP ---
    glm::vec3 lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    if (abs(currentLightDir.z) > 0.99f) {
        lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Le soleil regarde maintenant le point cible devant toi !
    glm::mat4 lightViewMatrix = glm::lookAt(lightPos, targetPos, lightUp);
    glm::mat4 lightSpaceMatrix = lightProjection * lightViewMatrix;

    // =====================================================
    // 2. PASSE 1 : SHADOW MAP (Depth Pass)
    // =====================================================
    // A. Sauvegarde de l'état du Viewport de l'Éditeur
    GLint previousFBO;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFBO);
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    // B. Activation du Framebuffer d'Ombre
    s_Data->ShadowFramebuffer->Bind();
    glClear(GL_DEPTH_BUFFER_BIT);

    // C. Activation du Culling des faces avant (Astuce anti "Shadow Acne")
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    s_Data->ShadowShader->Use();
    s_Data->ShadowShader->SetMat4("uLightSpaceMatrix", lightSpaceMatrix);

    auto view = scene->m_Registry.view<TransformComponent, MeshComponent>();
    for (auto entityID : view) {
        auto [transform, mesh] = view.get<TransformComponent, MeshComponent>(entityID);
        if (mesh.MeshData) {
            Entity entityObj{ entityID, scene };
            glm::mat4 globalTransform = scene->GetWorldTransform(entityObj);
            s_Data->ShadowShader->SetMat4("uModel", globalTransform);
            mesh.MeshData->Draw();
        }
    }

    // D. On nettoie l'état après le rendu de l'ombre
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);

    // =====================================================
    // 3. PASSE 2 : RENDU NORMAL (Lighting Pass)
    // =====================================================
    // A. On restaure le Viewport de l'Éditeur
    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if (s_Data->MainShader) {
        s_Data->MainShader->Use();
        s_Data->MainShader->SetInt("uRenderMode", renderMode);
    }

    if (renderMode == 2) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // B. Rendu de la scène classique
    for (auto entityID : view) {
        auto [transform, mesh] = view.get<TransformComponent, MeshComponent>(entityID);
        if (mesh.MeshData) {
            Entity entityObj{ entityID, scene };
            glm::mat4 globalTransform = scene->GetWorldTransform(entityObj);

            Shader* activeShader = s_Data->MainShader.get();
            activeShader->Use();

            if (entityObj.HasComponent<MaterialComponent>()) {
                auto& mat = entityObj.GetComponent<MaterialComponent>();
                if (mat.ShaderInstance) {
                    activeShader = mat.ShaderInstance.get();
                    activeShader->Use();

                    activeShader->SetMat4("uView", s_Data->CurrentView);
                    activeShader->SetMat4("uProjection", s_Data->CurrentProjection);

                    activeShader->SetVec3("uLightPos", glm::vec3(1.0f, 1.0f, 1.0f));
                    activeShader->SetVec3("uLightColor", glm::vec3(3.0f, 3.0f, 3.0f));

                    glm::mat4 invView = glm::inverse(s_Data->CurrentView);
                    glm::vec3 camPos = glm::vec3(invView[3]);
                    activeShader->SetVec3("uViewPos", camPos);

                    for (auto const& [name, val] : mat.FloatOverrides) activeShader->SetFloat(name, val);
                    for (auto const& [name, val] : mat.ColorOverrides) activeShader->SetVec3(name, val);

                    int slot = 0;
                    for (auto const& [name, texID] : mat.TextureOverrides) {
                        glActiveTexture(GL_TEXTURE0 + slot);
                        glBindTexture(GL_TEXTURE_2D, texID);
                        activeShader->SetInt(name, slot);
                        slot++;
                    }
                    for (auto const& [nodeID, texID] : mat.Textures) {
                        glActiveTexture(GL_TEXTURE0 + slot);
                        glBindTexture(GL_TEXTURE_2D, texID);
                        activeShader->SetInt("u_Tex_" + std::to_string(nodeID), slot);
                        slot++;
                    }
                }
            }

            // --- INJECTION DE LA SHADOW MAP (On utilise le slot 15 pour ne pas écraser les textures du mat) ---
            glActiveTexture(GL_TEXTURE15);
            glBindTexture(GL_TEXTURE_2D, s_Data->ShadowFramebuffer->GetDepthAttachmentRendererID());
            activeShader->SetInt("uShadowMap", 15);
            activeShader->SetMat4("uLightSpaceMatrix", lightSpaceMatrix);

            activeShader->SetVec3("uLightColor", currentLightColor);
            activeShader->SetVec3("uLightDir", currentLightDir);
            activeShader->SetFloat("uAmbientStrength", currentAmbient);
            activeShader->SetFloat("uDiffuseStrength", currentDiffuse);

            glm::vec3 colorVal = glm::vec3(1.0f);
            if (entityObj.HasComponent<ColorComponent>()) colorVal = entityObj.GetComponent<ColorComponent>().Color;

            activeShader->SetVec3("uColor", colorVal);
            activeShader->SetMat4("uModel", globalTransform);
            activeShader->SetInt("uEntityID", (int)entityID);
            activeShader->SetInt("uRenderMode", renderMode);

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
    if (!s_Data->LineShader) return; // On utilise le LineShader !

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilMask(0xFF);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    // --- LE FIX EST ICI : On utilise un shader basique ---
    s_Data->LineShader->Use();
    s_Data->LineShader->SetMat4("uModel", transform);
    s_Data->LineShader->SetMat4("uView", s_Data->CurrentView);
    s_Data->LineShader->SetMat4("uProjection", s_Data->CurrentProjection);
}

void Renderer::BeginOutlineDraw(const glm::mat4& transform, const glm::vec3& color) {
    if (!s_Data->LineShader) return;

    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilMask(0x00);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_DEPTH_TEST);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(6.0f); // Épaisseur de ton outline

    // --- LE FIX EST ICI : Couleur pure, sans influence des textures ! ---
    s_Data->LineShader->Use();
    s_Data->LineShader->SetMat4("uModel", transform);
    s_Data->LineShader->SetMat4("uView", s_Data->CurrentView);
    s_Data->LineShader->SetMat4("uProjection", s_Data->CurrentProjection);
    s_Data->LineShader->SetVec3("uColor", color);
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

void Renderer::SetShadowResolution(uint32_t resolution) {
    if (s_Data && s_Data->ShadowFramebuffer) {
        s_Data->ShadowFramebuffer->Resize(resolution, resolution);
    }
}