#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include "renderer/TextureLoader.h"

#define CE_ASSET()
using CHT_AssetBlocker = int;

// L'INTERFACE
class IAssetType {
public:
    virtual ~IAssetType() = default;
    virtual std::string GetName() const = 0;
    virtual std::string GetExtension() const = 0;
    virtual ImVec4 GetColor() const = 0;
    virtual std::string GetIconPath() const = 0;

    // --- NOUVEAU : Chaque asset sait comment s'auto-construire ! ---
    virtual void CreateDefaultAsset(const std::filesystem::path& path) const {
        std::ofstream f(path); // Par défaut : Crée un fichier vide
        f.close();
    }
};

// LA STRUCTURE DE DONNÉES
struct AssetTypeInfo {
    std::string Name;
    std::string Extension;
    ImVec4 Color;
    std::string IconPath;
    uint32_t IconID = 0;

    // --- NOUVEAU : On garde une trace de l'instance pour exécuter la création ---
    std::shared_ptr<IAssetType> Instance;
};

// LE REGISTRE
class AssetRegistry {
public:
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

        info.Instance = assetType; // <-- TRÈS IMPORTANT

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