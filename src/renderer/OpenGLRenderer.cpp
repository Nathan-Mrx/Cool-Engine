#include "OpenGLRenderer.h"
#include "../ecs/Components.h"
#include <glad/glad.h>

uint32_t OpenGLRenderer::GetIrradianceMapID() { return m_Data->IrradianceMap; }


void OpenGLRenderer::Init() {
    m_Data = new RendererData();

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    m_Data->MainShader = std::make_unique<Shader>("shaders/default.vert", "shaders/default.frag");
    m_Data->GridShader = std::make_unique<Shader>("shaders/grid.vert", "shaders/grid.frag");
    m_Data->LineShader = std::make_unique<Shader>("shaders/line.vert", "shaders/line.frag");
    m_Data->OutlineShader = std::make_unique<Shader>("shaders/outline.vert", "shaders/outline.frag");

    // shadow
    m_Data->ShadowShader = std::make_unique<Shader>("shaders/shadow.vert", "shaders/shadow.frag");

    m_Data->SkyboxShader = std::make_unique<Shader>("shaders/skybox.vert", "shaders/skybox.frag");

    m_Data->DDGIUpdateShader = std::make_unique<Shader>("shaders/ddgi_update.comp");

    float skyboxVertices[] = {
        // Face Arrière
        -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        // Face Gauche
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        // Face Droite
         1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
        // Face Avant
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        // Face Haut
        -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
        // Face Bas
        -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
    };

    glGenVertexArrays(1, &m_Data->SkyboxVAO);
    glGenBuffers(1, &m_Data->SkyboxVBO);
    glBindVertexArray(m_Data->SkyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_Data->SkyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // --- AJOUTE LA TEXTURE NOIRE PAR DEFAUT POUR LE PBR ---
    glGenTextures(1, &m_Data->IrradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->IrradianceMap);
    for (unsigned int i = 0; i < 6; ++i) {
        float black[3] = {0.0f, 0.0f, 0.0f};
        // On utilise GL_RGB32F comme ce qu'on avait corrigé pour le HDR
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F, 1, 1, 0, GL_RGB, GL_FLOAT, black);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    FramebufferSpecification shadowSpec;
    shadowSpec.Width = 2048;  // Haute résolution (2K) pour des ombres nettes
    shadowSpec.Height = 2048;
    shadowSpec.DepthOnly = true; // Mode spécial qu'on a codé juste avant !
    shadowSpec.Layers = 3;   // <-- LE NIVEAU AAA EST ICI
    m_Data->ShadowFramebuffer = std::make_unique<Framebuffer>(shadowSpec);

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

    glGenVertexArrays(1, &m_Data->GridVAO);
    glGenBuffers(1, &m_Data->GridVBO);

    glBindVertexArray(m_Data->GridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_Data->GridVBO);
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

    glGenVertexArrays(1, &m_Data->DebugBoxVAO);
    glGenBuffers(1, &m_Data->DebugBoxVBO);
    glBindVertexArray(m_Data->DebugBoxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_Data->DebugBoxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(boxVertices), boxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    m_Data->BRDFShader = std::make_unique<Shader>("shaders/brdf.vert", "shaders/brdf.frag");

    glGenTextures(1, &m_Data->BRDFLUTTexture);
    glBindTexture(GL_TEXTURE_2D, m_Data->BRDFLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned int captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO); glGenRenderbuffers(1, &captureRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Data->BRDFLUTTexture, 0);

    glViewport(0, 0, 512, 512);
    m_Data->BRDFShader->Use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    unsigned int emptyVAO; glGenVertexArrays(1, &emptyVAO); glBindVertexArray(emptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDeleteVertexArrays(1, &emptyVAO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &captureFBO); glDeleteRenderbuffers(1, &captureRBO);

    // --- INITIALISATION DE LA DDGI ---
    // On crée une grille de 16x8x16 = 2048 sondes !
    // Elles sont espacées de 2 mètres (200 cm) les unes des autres
    m_Data->GlobalDDGIVolume = std::make_unique<DDGIVolume>(
        glm::vec3(-1600.0f, 100.0f, -1600.0f), // Point de départ (coin bas-gauche)
        glm::ivec3(16, 8, 16),                 // Nombre de sondes
        glm::vec3(200.0f, 200.0f, 200.0f)      // Espacement Z-UP centimétrique !
    );
}


void OpenGLRenderer::RenderScene(Scene* scene, int renderMode) {
    // =====================================================
    // 0. LECTURE DE LA SKYBOX
    // =====================================================
    std::string activeSkyboxPath = "";
    float skyboxIntensity = 0.5f; // Valeurs par défaut
    float skyboxRotation = 0.0f;

    auto skyboxView = scene->m_Registry.view<SkyboxComponent>();
    for (auto entity : skyboxView) {
        auto& skybox = skyboxView.get<SkyboxComponent>(entity);
        activeSkyboxPath = skybox.HDRPath;
        skyboxIntensity = skybox.Intensity;
        skyboxRotation = skybox.Rotation;
        break;
    }

    UpdateSkybox(activeSkyboxPath);

    // =====================================================
    // 1. LECTURE GLOBALE DE LA LUMIÈRE
    // =====================================================
    auto lightView = scene->m_Registry.view<TransformComponent, DirectionalLightComponent>();

    glm::vec3 currentLightColor = glm::vec3(1.0f);
    glm::vec3 currentLightDir = glm::vec3(0.0f, 0.0f, -1.0f);
    float currentAmbient = 1.0f;
    float currentDiffuse = 0.0f;

    for (auto entity : lightView) {
        auto [lightTransform, light] = lightView.get<TransformComponent, DirectionalLightComponent>(entity);
        glm::vec3 baseDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        currentLightDir = glm::normalize(lightTransform.Rotation * baseDirection);
        currentLightColor = light.Color;
        currentAmbient = light.AmbientIntensity;
        currentDiffuse = light.DiffuseIntensity;
        break;
    }

    glm::mat4 invView = glm::inverse(m_Data->CurrentView);
    glm::vec3 camPos = glm::vec3(invView[3]);
    glm::vec3 camFront = -glm::normalize(glm::vec3(invView[2]));

    // --- FIX Z-UP ---
    glm::vec3 lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    if (abs(currentLightDir.z) > 0.99f) lightUp = glm::vec3(0.0f, 1.0f, 0.0f);

    // =====================================================
    // 2. PRÉPARATION DES 3 CASCADES (CSM)
    // =====================================================
    struct CascadeDef { float targetDist; float orthoSize; float farPlane; };
    CascadeDef cascades[3] = {
        { 750.0f,  1500.0f,  5000.0f },   // C0: Cible à 7.5m, Boîte de 15m de large
        { 2500.0f, 5000.0f,  10000.0f },  // C1: Cible à 25m, Boîte de 50m de large
        { 7500.0f, 15000.0f, 25000.0f }   // C2: Cible à 75m, Boîte de 150m de large
    };

    // Les distances de coupure pour le shader (où on passe d'une cascade à l'autre)
    float cascadeDistances[3] = { 1500.0f, 5000.0f, 15000.0f };
    std::vector<glm::mat4> lightSpaceMatrices;

    for (int i = 0; i < 3; i++) {
        glm::vec3 targetPos = camPos + camFront * cascades[i].targetDist;

        // --- FIX MATHÉMATIQUE : On utilise -farPlane en zNear pour TOUT englober ---
        glm::mat4 lightProjection = glm::ortho(
            -cascades[i].orthoSize, cascades[i].orthoSize,
            -cascades[i].orthoSize, cascades[i].orthoSize,
            -cascades[i].farPlane, cascades[i].farPlane // De très loin derrière à très loin devant !
        );

        // On place la caméra virtuelle directement sur le point cible
        glm::vec3 lightPos = targetPos;

        // Et elle regarde simplement dans la direction de la lumière
        glm::mat4 lightViewMatrix = glm::lookAt(lightPos, lightPos + currentLightDir, lightUp);

        lightSpaceMatrices.push_back(lightProjection * lightViewMatrix);
    }

    // =====================================================
    // 3. PASSE 1 : SHADOW MAPS (Depth Pass x3)
    // =====================================================
    GLint previousFBO;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFBO);
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    m_Data->ShadowFramebuffer->Bind();
    m_Data->ShadowShader->Use();

    auto viewMeshes = scene->m_Registry.view<TransformComponent, MeshComponent>();

    // On boucle sur nos 3 couches !
    for (int i = 0; i < 3; i++) {
        // Magie : on connecte la tranche 'i' au Framebuffer
        m_Data->ShadowFramebuffer->BindDepthLayer(i);
        glClear(GL_DEPTH_BUFFER_BIT); // On nettoie uniquement cette tranche

        m_Data->ShadowShader->SetMat4("uLightSpaceMatrix", lightSpaceMatrices[i]);

        for (auto entityID : viewMeshes) {
            auto [transform, mesh] = viewMeshes.get<TransformComponent, MeshComponent>(entityID);
            if (mesh.MeshData) {
                Entity entityObj{ entityID, scene };
                m_Data->ShadowShader->SetMat4("uModel", scene->GetWorldTransform(entityObj));
                mesh.MeshData->Draw();
            }
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

    if (m_Data->MainShader) {
        m_Data->MainShader->Use();
        m_Data->MainShader->SetInt("uRenderMode", renderMode);
    }

    if (renderMode == 2) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // B. Rendu de la scène classique
    for (auto entityID : viewMeshes) {
        auto [transform, mesh] = viewMeshes.get<TransformComponent, MeshComponent>(entityID);
        if (mesh.MeshData) {
            Entity entityObj{ entityID, scene };
            glm::mat4 globalTransform = scene->GetWorldTransform(entityObj);

            Shader* activeShader = m_Data->MainShader.get();
            activeShader->Use();

            if (entityObj.HasComponent<MaterialComponent>()) {
                auto& mat = entityObj.GetComponent<MaterialComponent>();
                if (mat.ShaderInstance) {
                    activeShader = mat.ShaderInstance.get();
                    activeShader->Use();

                    activeShader->SetMat4("uView", m_Data->CurrentView);
                    activeShader->SetMat4("uProjection", m_Data->CurrentProjection);

                    activeShader->SetVec3("uLightPos", glm::vec3(1.0f, 1.0f, 1.0f));
                    activeShader->SetVec3("uLightColor", glm::vec3(3.0f, 3.0f, 3.0f));

                    glm::mat4 invView = glm::inverse(m_Data->CurrentView);
                    glm::vec3 camPos = glm::vec3(invView[3]);
                    activeShader->SetVec3("uViewPos", camPos);

                    // --- AJOUT DU PRÉFIXE "u_" POUR LES INSTANCES ---
                    for (auto const& [name, val] : mat.FloatOverrides)
                        activeShader->SetFloat("u_" + name, val);

                    for (auto const& [name, val] : mat.ColorOverrides)
                        activeShader->SetVec3("u_" + name, val);

                    int slot = 0;
                    for (auto const& [name, texID] : mat.TextureOverrides) {
                        glActiveTexture(GL_TEXTURE0 + slot);
                        glBindTexture(GL_TEXTURE_2D, texID);
                        activeShader->SetInt("u_" + name, slot); // <-- LE FIX EST ICI
                        slot++;
                    }

                    // (Les textures de base utilisent déjà "u_Tex_" donc ne change pas cette boucle :)
                    for (auto const& [nodeID, texID] : mat.Textures) {
                        glActiveTexture(GL_TEXTURE0 + slot);
                        glBindTexture(GL_TEXTURE_2D, texID);
                        activeShader->SetInt("u_Tex_" + std::to_string(nodeID), slot);
                        slot++;
                    }
                }
            }

            // --- INJECTION DU CSM (CASCADED SHADOW MAPS) ---
            glActiveTexture(GL_TEXTURE15);
            // ATTENTION : C'est un GL_TEXTURE_2D_ARRAY maintenant !
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_Data->ShadowFramebuffer->GetDepthAttachmentRendererID());
            activeShader->SetInt("uShadowMap", 15);

            // ==========================================================
            // --- INJECTION DE LA DDGI (LUMIÈRE GLOBALE DYNAMIQUE) ---
            // ==========================================================
            glActiveTexture(GL_TEXTURE14);
            if (m_Data->GlobalDDGIVolume) {
                // On attache notre Atlas 2D géant au port 14
                glBindTexture(GL_TEXTURE_2D, m_Data->GlobalDDGIVolume->GetIrradianceTexture());
                activeShader->SetInt("uDDGIIrradiance", 14);

                // On envoie les coordonnées de la grille pour que le shader puisse se repérer
                glm::vec3 startPos = m_Data->GlobalDDGIVolume->GetStartPosition();
                glm::vec3 spacing  = m_Data->GlobalDDGIVolume->GetProbeSpacing();
                glm::ivec3 counts  = m_Data->GlobalDDGIVolume->GetProbeCount();

                activeShader->SetFloat("uDDGIStartX", startPos.x);
                activeShader->SetFloat("uDDGIStartY", startPos.y);
                activeShader->SetFloat("uDDGIStartZ", startPos.z);

                activeShader->SetFloat("uDDGISpaceX", spacing.x);
                activeShader->SetFloat("uDDGISpaceY", spacing.y);
                activeShader->SetFloat("uDDGISpaceZ", spacing.z);

                activeShader->SetInt3("uDDGIProbeCount", counts.x, counts.y, counts.z);
            }

            // --- INJECTION DES REFLETS SPÉCULAIRES (IBL COMPLET) ---
            glActiveTexture(GL_TEXTURE12);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->PrefilterMap);
            activeShader->SetInt("uPrefilterMap", 12);

            glActiveTexture(GL_TEXTURE13);
            glBindTexture(GL_TEXTURE_2D, m_Data->BRDFLUTTexture);
            activeShader->SetInt("uBRDFLUT", 13);


            // --- NOUVEAU : ENVOI DES RÉGLAGES DE LA SKYBOX ---
            activeShader->SetFloat("u_SkyboxIntensity", skyboxIntensity);
            activeShader->SetFloat("u_SkyboxRotation", glm::radians(skyboxRotation));

            // On envoie les tableaux de matrices et de distances au Shader
            for (int i = 0; i < 3; i++) {
                activeShader->SetMat4("uLightSpaceMatrices[" + std::to_string(i) + "]", lightSpaceMatrices[i]);
                activeShader->SetFloat("uCascadeDistances[" + std::to_string(i) + "]", cascadeDistances[i]);
            }

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

    // =====================================================
    // 4. PASSE 3 : SKYBOX (Dernière étape pour optimiser les perfs)
    // =====================================================
    glDepthFunc(GL_LEQUAL); // Important ! Laisse passer les pixels à la profondeur 1.0 exacte

    // =====================================================
    // 4. PASSE 3 : SKYBOX (Dernière étape pour optimiser les perfs)
    // =====================================================
    glDepthFunc(GL_LEQUAL);

    if (!activeSkyboxPath.empty() && m_Data->SkyboxShader && m_Data->EnvironmentMapID) {
        m_Data->SkyboxShader->Use();

        // --- LE FIX DE L'ÉCHELLE (CENTIMÈTRES) ---
        // On détruit la translation de la caméra (mat3 -> mat4)
        // pour que le ciel soit gigantesque et suive toujours le joueur !
        glm::mat4 skyboxView = glm::mat4(glm::mat3(m_Data->CurrentView));

        m_Data->SkyboxShader->SetMat4("uView", skyboxView);
        m_Data->SkyboxShader->SetMat4("uProjection", m_Data->CurrentProjection);

        // --- NOUVEAU : ENVOI DES RÉGLAGES ---
        m_Data->SkyboxShader->SetFloat("uIntensity", skyboxIntensity);
        m_Data->SkyboxShader->SetFloat("uRotation", glm::radians(skyboxRotation));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_Data->EnvironmentMapID);
        m_Data->SkyboxShader->SetInt("uEquirectangularMap", 0);

        glBindVertexArray(m_Data->SkyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    glDepthFunc(GL_LESS); // Remet la profondeur normale par sécurité

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void OpenGLRenderer::Shutdown() {
    // Nettoyage des shaders et du pointeur m_Data
    m_Data->MainShader.reset();
    m_Data->GridShader.reset();
    glDeleteVertexArrays(1, &m_Data->GridVAO);
    glDeleteBuffers(1, &m_Data->GridVBO);

    delete m_Data;
}

void OpenGLRenderer::BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos) {
    // 1. Sauvegarde des matrices pour les autres shaders (comme la grille)
    m_Data->CurrentView = view;
    m_Data->CurrentProjection = projection;

    // 2. Activer le shader principal
    m_Data->MainShader->Use();

    // 3. Envoyer les matrices au shader principal
    m_Data->MainShader->SetMat4("uView", view);
    m_Data->MainShader->SetMat4("uProjection", projection);

    // 4. Envoyer la position de la caméra
    m_Data->MainShader->SetVec3("uCameraPos", camPos);

    // 5. Configuration de l'état OpenGL
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OpenGLRenderer::EndScene() {
    // Ici, tu pourrais ajouter des effets de post-process
    // ou simplement nettoyer l'état si nécessaire.
}

void OpenGLRenderer::DrawGrid(bool show) {
    if (!show || !m_Data->GridShader) return;

    // ... (glEnable, etc.)
    m_Data->GridShader->Use();

    // IL FAUT RENVOYER LES MATRICES ICI AUSSI
    // On peut les stocker dans m_Data lors du BeginScene pour les réutiliser
    m_Data->GridShader->SetMat4("uView", m_Data->CurrentView);
    m_Data->GridShader->SetMat4("uProjection", m_Data->CurrentProjection);

    glBindVertexArray(m_Data->GridVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // ...
}

void OpenGLRenderer::Clear() {
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    // AJOUT DU GL_STENCIL_BUFFER_BIT ICI :
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OpenGLRenderer::DrawDebugArrow(const glm::vec3& start, const glm::vec3& forward, const glm::vec3& right, const glm::vec3& up, const glm::vec3& color, const glm::mat4& view, const glm::mat4& projection, float length) {

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
    if (m_Data->LineShader) {
        m_Data->LineShader->Use();
        m_Data->LineShader->SetMat4("uModel", glm::mat4(1.0f));

        // Attention à bien utiliser les noms que tu mettras dans line.vert !
        // J'utilise "uView" et "uProjection" car c'est ta convention dans MainShader.
        m_Data->LineShader->SetMat4("uView", view);
        m_Data->LineShader->SetMat4("uProjection", projection);
        m_Data->LineShader->SetVec3("uColor", color);
    }

    glDrawArrays(GL_LINES, 0, 10);

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
}

void OpenGLRenderer::BeginOutlineMask(const glm::mat4& transform) {
    if (!m_Data->LineShader) return; // On utilise le LineShader !

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilMask(0xFF);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    // --- LE FIX EST ICI : On utilise un shader basique ---
    m_Data->LineShader->Use();
    m_Data->LineShader->SetMat4("uModel", transform);
    m_Data->LineShader->SetMat4("uView", m_Data->CurrentView);
    m_Data->LineShader->SetMat4("uProjection", m_Data->CurrentProjection);
}

void OpenGLRenderer::BeginOutlineDraw(const glm::mat4& transform, const glm::vec3& color) {
    if (!m_Data->LineShader) return;

    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilMask(0x00);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_DEPTH_TEST);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(6.0f); // Épaisseur de ton outline

    // --- LE FIX EST ICI : Couleur pure, sans influence des textures ! ---
    m_Data->LineShader->Use();
    m_Data->LineShader->SetMat4("uModel", transform);
    m_Data->LineShader->SetMat4("uView", m_Data->CurrentView);
    m_Data->LineShader->SetMat4("uProjection", m_Data->CurrentProjection);
    m_Data->LineShader->SetVec3("uColor", color);
}

void OpenGLRenderer::EndOutline() {
    // Nettoyage absolu pour ne pas corrompre la frame suivante
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);

    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glDisable(GL_STENCIL_TEST);

    // On réactive le Z-Buffer pour le rendu normal !
    glEnable(GL_DEPTH_TEST);
}

void OpenGLRenderer::DrawDebugBox(const glm::mat4& transform, const glm::vec3& color) {
    if (!m_Data->LineShader) return;

    m_Data->LineShader->Use();
    m_Data->LineShader->SetMat4("uModel", transform);
    m_Data->LineShader->SetMat4("uView", m_Data->CurrentView);
    m_Data->LineShader->SetMat4("uProjection", m_Data->CurrentProjection);
    m_Data->LineShader->SetVec3("uColor", color);

    glBindVertexArray(m_Data->DebugBoxVAO);
    glDrawArrays(GL_LINES, 0, 24); // 12 lignes * 2 sommets = 24
}

void OpenGLRenderer::SetShadowResolution(uint32_t resolution) {
    if (m_Data && m_Data->ShadowFramebuffer) {
        m_Data->ShadowFramebuffer->Resize(resolution, resolution);
    }
}

void OpenGLRenderer::UpdateSkybox(const std::string& hdrPath) {
    if (m_Data->CurrentSkyboxPath == hdrPath) return; // Déjà chargée
    m_Data->CurrentSkyboxPath = hdrPath;

    if (hdrPath.empty()) return;

    uint32_t newEnvMap = TextureLoader::LoadHDR(hdrPath.c_str());
    if (newEnvMap == 0) return;

    // Sauvegarde du FBO de l'éditeur pour ne pas casser l'affichage
    GLint previousFBO;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFBO);
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    // Nettoyage des anciennes textures
    if (m_Data->EnvironmentMapID) glDeleteTextures(1, &m_Data->EnvironmentMapID);
    if (m_Data->EnvCubemap) glDeleteTextures(1, &m_Data->EnvCubemap);
    // (L'IrradianceMap de base sera écrasée plus bas, pas besoin de la delete si on réutilise l'ID, mais c'est plus propre)
    if (m_Data->IrradianceMap) glDeleteTextures(1, &m_Data->IrradianceMap);

    m_Data->EnvironmentMapID = newEnvMap;

    if (!m_Data->EquirectToCubeShader) {
        m_Data->EquirectToCubeShader = std::make_unique<Shader>("shaders/cubemap.vert", "shaders/equirect_to_cube.frag");
        m_Data->IrradianceShader = std::make_unique<Shader>("shaders/cubemap.vert", "shaders/irradiance.frag");
        m_Data->PrefilterShader = std::make_unique<Shader>("shaders/cubemap.vert", "shaders/prefilter.frag");
    }

    unsigned int captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    // ========================================================
    // --- FIX 2 : ON REMET LA CONFIGURATION DU FBO ET LE CUBEMAP ---
    // ========================================================
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    glGenTextures(1, &m_Data->EnvCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->EnvCubemap);
    for (unsigned int i = 0; i < 6; ++i) {
        // En 32F pour encaisser la lumière du soleil !
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // ========================================================

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    // --- LE FIX DES COUTURES (Les matrices strictement Y-UP pour OpenGL) ---
    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    m_Data->EquirectToCubeShader->Use();
    m_Data->EquirectToCubeShader->SetInt("uEquirectangularMap", 0);
    m_Data->EquirectToCubeShader->SetMat4("uProjection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_Data->EnvironmentMapID);

    glViewport(0, 0, 512, 512);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i) {
        m_Data->EquirectToCubeShader->SetMat4("uView", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Data->EnvCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(m_Data->SkyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    // 2. CONVOLUTION (IRRADIANCE)
    glGenTextures(1, &m_Data->IrradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->IrradianceMap);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 3. PREFILTER MAP (Les reflets)
    if (m_Data->PrefilterMap) glDeleteTextures(1, &m_Data->PrefilterMap);
    glGenTextures(1, &m_Data->PrefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->PrefilterMap);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F, 128, 128, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    m_Data->PrefilterShader->Use();
    m_Data->PrefilterShader->SetInt("uEnvironmentMap", 0);
    m_Data->PrefilterShader->SetMat4("uProjection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->EnvCubemap);

    unsigned int maxMipLevels = 5;
    for (unsigned int mip = 0; mip < maxMipLevels; ++mip) {
        unsigned int mipWidth  = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        unsigned int mipHeight = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (float)mip / (float)(maxMipLevels - 1);
        m_Data->PrefilterShader->SetFloat("uRoughness", roughness);
        for (unsigned int i = 0; i < 6; ++i) {
            m_Data->PrefilterShader->SetMat4("uView", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Data->PrefilterMap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindVertexArray(m_Data->SkyboxVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);

    m_Data->IrradianceShader->Use();
    m_Data->IrradianceShader->SetInt("uEnvironmentMap", 0);
    m_Data->IrradianceShader->SetMat4("uProjection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->EnvCubemap);

    glViewport(0, 0, 32, 32);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i) {
        m_Data->IrradianceShader->SetMat4("uView", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Data->IrradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(m_Data->SkyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    // =========================================================================
    // --- MISE À JOUR DU VOLUME DDGI (LANCEMENT DU COMPUTE SHADER) ---
    // =========================================================================
    if (m_Data->GlobalDDGIVolume && m_Data->DDGIUpdateShader) {
        m_Data->DDGIUpdateShader->Use();

        // On envoie la Skybox fraîchement générée pour l'échantillonnage
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_Data->EnvCubemap);
        m_Data->DDGIUpdateShader->SetInt("uEnvironmentMap", 0);

        glm::ivec3 probeCount = m_Data->GlobalDDGIVolume->GetProbeCount();
        m_Data->DDGIUpdateShader->SetInt3("uProbeCount", probeCount.x, probeCount.y, probeCount.z);

        // On connecte la texture de notre volume en mode "Ecriture Pure" (Image Load/Store)
        // Format GL_RGBA16F, Unité d'image 0
        glBindImageTexture(0, m_Data->GlobalDDGIVolume->GetIrradianceTexture(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

        // BOUM ! On lance 2048 groupes de travail en parallèle !
        // (La carte graphique va traiter plus de 130 000 pixels quasi-instantanément)
        m_Data->DDGIUpdateShader->Dispatch(probeCount.x, probeCount.y, probeCount.z);

        // On oblige le GPU à attendre que tous les threads aient fini d'écrire
        // avant d'autoriser le reste du moteur à lire cette texture.
        Shader::IssueMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    // Nettoyage classique
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}