#pragma once
#include "../IAssetEditor.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include "renderer/Shader.h"
#include "renderer/OpenGLFramebuffer.h"
#include "renderer/Mesh.h"

struct MIParameter {
    std::string Type;
    std::string Name;
    std::string Category = "General";

    float FloatVal = 0.0f;
    bool BoolVal = false;
    glm::vec4 ColorVal = {1.0f, 1.0f, 1.0f, 1.0f};
    std::string TexturePath = "";
    void* TextureID = nullptr;

    bool IsOverridden = false;
    bool IsVisible = false;
};

struct MIStaticTexture {
    std::string UniformName;
    void* TextureID = nullptr;
};

class MaterialInstanceEditorPanel : public IAssetEditor {
public:
    MaterialInstanceEditorPanel() = default;
    ~MaterialInstanceEditorPanel() = default;

    void OpenAsset(const std::filesystem::path& path); // Fonction que IAssetEditor devrait idéalement avoir
    void Load(const std::filesystem::path& path);

    void OnImGuiRender(bool& isOpen) override;
    void Save() override;
    void SaveAs() override {} // À implémenter plus tard si besoin

    // Callbacks
    std::function<void(const std::filesystem::path&)> OnMaterialInstanceSavedCallback;

    // --- ACCESSEURS POUR LE SYSTEME D'UNDO ---
    const std::filesystem::path& GetCurrentPath() const { return m_CurrentPath; }

    // ON REMPLACE std::map PAR std::unordered_map ICI :
    void SetFullState(const std::string& parentPath, const std::unordered_map<std::string, MIParameter>& params) {
        bool parentChanged = (m_ParentMaterialPath != parentPath);
        m_ParentMaterialPath = parentPath;
        if (parentChanged) LoadParentParameters();

        m_Parameters = params;
        EvaluateParameterVisibility();
        CompilePreviewShader();
        Save(); // Auto-save lors de l'Undo !
    }

    void OnUpdate(float deltaTime);

private:
    void LoadParentParameters();
    void CompilePreviewShader();

    void ResetParameterToDefault(const std::string& paramName);
    void EvaluateParameterVisibility();

    // --- SOUS-FONCTIONS DE RENDU (Refactoring) ---
    void DrawPreviewColumn();
    void RenderPreview3D();
    void DrawDetailsColumn();
    void HandleDragAndDropParent();
    void DrawParameters();

private:
    std::filesystem::path m_CurrentPath;
    std::string m_ParentMaterialPath = "";
    nlohmann::json m_ParentGraphJson;

    std::unordered_map<std::string, MIParameter> m_Parameters;
    std::vector<MIStaticTexture> m_StaticTextures;

    // --- Cached Parent Defaults ---
    void* m_DefaultAlbedoTex = nullptr;
    void* m_DefaultNormalTex = nullptr;
    void* m_DefaultMetallicTex = nullptr;
    void* m_DefaultRoughnessTex = nullptr;
    void* m_DefaultAOTex = nullptr;

    glm::vec4 m_DefaultColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float m_DefaultMetallic = 0.0f;
    float m_DefaultRoughness = 0.5f;
    float m_DefaultAO = 1.0f;

    // --- Preview 3D ---
    std::shared_ptr<Framebuffer> m_PreviewFramebuffer;
    std::shared_ptr<Mesh> m_PreviewMesh;
    std::shared_ptr<Shader> m_PreviewShader;

    // Variables de la caméra/preview
    float m_RotationSpeed = 30.0f;
    float m_CameraDistance = 250.0f;
    float m_PreviewRotation = 0.0f;

    glm::vec2 m_ViewportSize = {800.0f, 600.0f};
};