#pragma once
#include "AssetRegistry.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

CE_ASSET()
struct SceneAsset : public IAssetType {
    std::string GetName() const override { return "Scene"; }
    std::string GetExtension() const override { return ".cescene"; }
    ImVec4 GetColor() const override { return ImVec4(0.2f, 0.4f, 0.8f, 1.0f); }
    std::string GetIconPath() const override { return "icons/scene.png"; }

    void CreateDefaultAsset(const std::filesystem::path& path) const override {
        auto tempScene = std::make_shared<Scene>();
        SceneSerializer serializer(tempScene);
        serializer.Serialize(path.string());
    }
};

CE_ASSET()
struct PrefabAsset : public IAssetType {
    std::string GetName() const override { return "Prefab"; }
    std::string GetExtension() const override { return ".ceprefab"; }
    ImVec4 GetColor() const override { return ImVec4(0.8f, 0.4f, 0.2f, 1.0f); }
    std::string GetIconPath() const override { return "icons/prefab.png"; }

    void CreateDefaultAsset(const std::filesystem::path& path) const override {
        auto tempScene = std::make_shared<Scene>();
        tempScene->CreateEntity(path.stem().string() + " Root");
        SceneSerializer serializer(tempScene);
        serializer.Serialize(path.string());
    }
};

CE_ASSET()
struct MaterialAsset : public IAssetType {
    std::string GetName() const override { return "Material"; }
    std::string GetExtension() const override { return ".cemat"; }
    ImVec4 GetColor() const override { return ImVec4(0.3f, 0.8f, 0.4f, 1.0f); }
    std::string GetIconPath() const override { return "icons/material.png"; }

    void CreateDefaultAsset(const std::filesystem::path& path) const override {
        std::ofstream file(path);
        file << "{\n    \"Type\": \"MaterialGraph\",\n    \"NextID\": 1,\n    \"Nodes\": [],\n    \"Links\": []\n}";
    }
};

// --- NOUVEAU ---
CE_ASSET()
struct AudioAsset : public IAssetType {
    std::string GetName() const override { return "Audio"; }
    std::string GetExtension() const override { return ".cewav"; }
    ImVec4 GetColor() const override { return ImVec4(0.8f, 0.2f, 0.5f, 1.0f); } // Violet !
    std::string GetIconPath() const override { return "icons/audio.png"; }
    // Pas besoin de CreateDefaultAsset : on utilise le fichier vide par défaut,
    // bien qu'en général on n'en crée pas à la main.
};