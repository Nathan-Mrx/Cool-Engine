#pragma once
#include "AssetRegistry.h"

CE_ASSET()
struct SceneAsset : public IAssetType {
    std::string GetName() const override { return "Scene"; }
    std::string GetExtension() const override { return ".cescene"; }
    ImVec4 GetColor() const override { return ImVec4(0.2f, 0.4f, 0.8f, 1.0f); }
    std::string GetIconPath() const override { return "icons/scene.png"; }
};

CE_ASSET()
struct PrefabAsset : public IAssetType {
    std::string GetName() const override { return "Prefab"; }
    std::string GetExtension() const override { return ".ceprefab"; }
    ImVec4 GetColor() const override { return ImVec4(0.8f, 0.4f, 0.2f, 1.0f); }
    std::string GetIconPath() const override { return "icons/prefab.png"; }
};

CE_ASSET()
struct MaterialAsset : public IAssetType {
    std::string GetName() const override { return "Material"; }
    std::string GetExtension() const override { return ".cemat"; }
    ImVec4 GetColor() const override { return ImVec4(0.3f, 0.8f, 0.4f, 1.0f); }
    std::string GetIconPath() const override { return "icons/material.png"; }
};

CE_ASSET()
struct AudioAsset : public IAssetType {
    std::string GetName() const override { return "Audio"; }
    std::string GetExtension() const override { return ".cewav"; }
    ImVec4 GetColor() const override { return ImVec4(0.8f, 0.2f, 0.5f, 1.0f); }
    std::string GetIconPath() const override { return "icons/audio.png"; }
};