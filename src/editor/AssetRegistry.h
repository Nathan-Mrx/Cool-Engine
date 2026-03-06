#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <imgui.h>
#include <filesystem>
#include "renderer/TextureLoader.h"

// --- LA MACRO POUR LE CHT ---
#define CE_ASSET()

// --- LE BOUCLIER ANTI-PYTHON ---
using CHT_AssetBlocker = int;

// Les données finales stockées
struct AssetTypeInfo {
    std::string Name;
    std::string Extension;
    ImVec4 Color;
    std::string IconPath;
    uint32_t IconID = 0;
};

// --- L'INTERFACE (Le contrat C++) ---
class IAssetType {
public:
    virtual ~IAssetType() = default;
    virtual std::string GetName() const = 0;
    virtual std::string GetExtension() const = 0;
    virtual ImVec4 GetColor() const = 0;
    virtual std::string GetIconPath() const = 0;
};

// --- LE REGISTRE ---
class AssetRegistry {
public:
    // Cette fonction sera générée par le Cool Header Tool !
    static void RegisterAllAssets();

    static std::unordered_map<std::string, AssetTypeInfo>& GetRegistry() {
        static std::unordered_map<std::string, AssetTypeInfo> registry;
        return registry;
    }

    static void Register(std::shared_ptr<IAssetType> assetType) {
        auto& reg = GetRegistry();
        AssetTypeInfo info;
        info.Name = assetType->GetName();
        info.Extension = assetType->GetExtension();
        info.Color = assetType->GetColor();
        info.IconPath = assetType->GetIconPath();

        // Préchargement de l'icône si elle existe
        if (!info.IconPath.empty() && std::filesystem::exists(info.IconPath)) {
            info.IconID = TextureLoader::LoadTexture(info.IconPath.c_str());
        }

        reg[info.Extension] = info;
    }

    static bool GetInfo(const std::string& extension, AssetTypeInfo& outInfo) {
        auto& reg = GetRegistry();
        if (reg.find(extension) != reg.end()) {
            outInfo = reg[extension];
            return true;
        }
        return false;
    }
};