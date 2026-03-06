#pragma once
#include "../IAssetEditor.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include "renderer/Shader.h"
#include "renderer/Framebuffer.h"
#include "renderer/Mesh.h"

struct MIParameter {
    std::string Type;
    std::string Name;

    float FloatVal = 0.0f;
    bool BoolVal = false;
    glm::vec4 ColorVal = {1.0f, 1.0f, 1.0f, 1.0f};
    std::string TexturePath = "";
    uint32_t TextureID = 0;

    bool IsOverridden = false;
};

struct MIStaticTexture {
    std::string UniformName;
    uint32_t TextureID = 0;
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

private:
    void LoadParentParameters();
    void CompilePreviewShader();

    void ResetParameterToDefault(const std::string& paramName);

private:
    std::filesystem::path m_CurrentPath;
    std::string m_ParentMaterialPath = "";
    std::unordered_map<std::string, MIParameter> m_Parameters;

    std::vector<MIStaticTexture> m_StaticTextures;

    // --- Preview 3D ---
    std::shared_ptr<Framebuffer> m_PreviewFramebuffer;
    std::shared_ptr<Shader> m_PreviewShader;
    std::shared_ptr<Mesh> m_PreviewMesh;

    float m_CameraDistance = 250.0f;
    float m_RotationSpeed = 30.0f;
};