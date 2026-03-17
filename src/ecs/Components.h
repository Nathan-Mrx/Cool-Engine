#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <filesystem>
#include <map>
#include <nfd.hpp>

#include "renderer/Mesh.h"
#include "../renderer/ModelLoader.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include "core/UUID.h"
#include "project/Project.h"
#include "renderer/Shader.h"
#include "renderer/TextureLoader.h"
#include "scene/ScriptableNode.h"

// =========================================================================================
// MACROS POUR LE COOL HEADER TOOL (CHT)
// =========================================================================================
#define CE_COMPONENT(...)
#define CE_PROPERTY(...)

// =========================================================================================
// EXTENSIONS DE SÉRIALISATION GLM, UUID & ENUMS
// =========================================================================================
namespace glm {
    inline void to_json(nlohmann::json& j, const vec3& v) { j = nlohmann::json::array({v.x, v.y, v.z}); }
    inline void from_json(const nlohmann::json& j, vec3& v) { v.x = j[0]; v.y = j[1]; v.z = j[2]; }
    inline void to_json(nlohmann::json& j, const quat& q) { j = nlohmann::json::array({q.w, q.x, q.y, q.z}); }
    inline void from_json(const nlohmann::json& j, quat& q) { q.w = j[0]; q.x = j[1]; q.y = j[2]; q.z = j[3]; }
}
namespace nlohmann {
    template <> struct adl_serializer<UUID> {
        static void to_json(json& j, const UUID& id) { j = (uint64_t)id; }
        static void from_json(const json& j, UUID& id) { id = j.get<uint64_t>(); }
    };
}

// Définit le comportement de l'objet dans le monde physique (Avec sérialisation auto en int pour rétrocompatibilité)
enum class RigidBodyType { Static = 0, Kinematic, Dynamic };
NLOHMANN_JSON_SERIALIZE_ENUM(RigidBodyType, {
    {RigidBodyType::Static, 0},
    {RigidBodyType::Kinematic, 1},
    {RigidBodyType::Dynamic, 2}
})


// =========================================================================================
// STRUCTURES DE DONNÉES DE BASE
// =========================================================================================
struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vector3() = default;
    Vector3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    glm::vec3 ToGlm() const { return { x, y, z }; }
    float* Data() { return &x; }
};


// =========================================================================================
// COMPOSANTS ECS
// =========================================================================================

CE_COMPONENT()
struct TagComponent {
    CE_PROPERTY() std::string Tag;

    TagComponent() = default;
    TagComponent(const std::string& tag) : Tag(tag) {}
};

CE_COMPONENT()
struct TransformComponent {
    CE_PROPERTY() glm::vec3 Location = { 0.0f, 0.0f, 0.0f };
    CE_PROPERTY() glm::vec3 RotationEuler = { 0.0f, 0.0f, 0.0f };
    CE_PROPERTY() glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    CE_PROPERTY() glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

    TransformComponent() = default;
    TransformComponent(const TransformComponent&) = default;
    TransformComponent(const glm::vec3& translation) : Location(translation) {}

    glm::mat4 GetTransform() const {
        return glm::translate(glm::mat4(1.0f), Location)
             * glm::toMat4(Rotation)
             * glm::scale(glm::mat4(1.0f), Scale);
    }

    glm::vec3 GetForwardVector() const { return glm::rotate(Rotation, glm::vec3(0.0f, 0.0f, 1.0f)); }
    glm::vec3 GetUpVector() const { return glm::rotate(Rotation, glm::vec3(0.0f, 1.0f, 0.0f)); }
    glm::vec3 GetRightVector() const { return glm::rotate(Rotation, glm::vec3(1.0f, 0.0f, 0.0f)); }
};

CE_COMPONENT()
struct ColorComponent {
    CE_PROPERTY() glm::vec3 Color = { 1.0f, 1.0f, 1.0f };

    ColorComponent() = default;
    ColorComponent(const glm::vec3& color) : Color(color) {}
};

CE_COMPONENT()
struct CameraComponent {
    CE_PROPERTY() bool Primary = true;
    CE_PROPERTY() float FOV = 90.0f;
    CE_PROPERTY() float NearClip = 0.1f;
    CE_PROPERTY() float FarClip = 100000.0f;

    CameraComponent() = default;
    CameraComponent(const CameraComponent&) = default;
};

CE_COMPONENT()
struct MeshComponent {
    std::shared_ptr<Mesh> MeshData; // Non sérialisé ! Généré à l'exécution.
    CE_PROPERTY() std::string AssetPath;
};

CE_COMPONENT()
struct DirectionalLightComponent {
    CE_PROPERTY() glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
    CE_PROPERTY() float AmbientIntensity = 0.2f;
    CE_PROPERTY() float DiffuseIntensity = 0.8f;
};

CE_COMPONENT()
struct RigidBodyComponent {
    CE_PROPERTY() RigidBodyType Type = RigidBodyType::Static;
    CE_PROPERTY() float Mass = 1.0f;

    uint32_t RuntimeBodyID = 0xFFFFFFFF; // Non sérialisé !

    RigidBodyComponent() = default;
    RigidBodyComponent(const RigidBodyComponent&) = default;
};

CE_COMPONENT()
struct BoxColliderComponent {
    CE_PROPERTY() glm::vec3 HalfSize = { 0.5f, 0.5f, 0.5f };
    CE_PROPERTY() glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
    CE_PROPERTY() float Friction = 0.5f;
    CE_PROPERTY() float Restitution = 0.0f;

    BoxColliderComponent() = default;
    BoxColliderComponent(const BoxColliderComponent&) = default;
};

CE_COMPONENT()
struct NativeScriptComponent {
    CE_PROPERTY() std::string ScriptName = "None";

    // Variables d'exécution dynamiques (Non sérialisées)
    ScriptableNode* Instance = nullptr;
    ScriptableNode* (*InstantiateScript)() = nullptr;
    void (*DestroyScript)(NativeScriptComponent*) = nullptr;

    template<typename T>
    void Bind() {
        InstantiateScript = []() { return static_cast<ScriptableNode*>(new T()); };
        DestroyScript = [](NativeScriptComponent* nsc) {
            delete nsc->Instance;
            nsc->Instance = nullptr;
        };
    }
};

CE_COMPONENT()
struct RelationshipComponent {
    // Note : Aucun CE_PROPERTY ici ! La sauvegarde de cet arbre complexe
    // est gérée manuellement par des templates dans SceneSerializer.cpp
    entt::entity Parent = entt::null;
    entt::entity FirstChild = entt::null;
    entt::entity PreviousSibling = entt::null;
    entt::entity NextSibling = entt::null;

    RelationshipComponent() = default;
    RelationshipComponent(const RelationshipComponent&) = default;
};

CE_COMPONENT()
struct IDComponent {
    CE_PROPERTY() UUID ID;

    IDComponent() = default;
    IDComponent(const IDComponent&) = default;
    IDComponent(UUID id) : ID(id) {}
};

CE_COMPONENT()
struct PrefabComponent {
    CE_PROPERTY() std::string PrefabPath;

    PrefabComponent() = default;
    PrefabComponent(const PrefabComponent&) = default;
    PrefabComponent(const std::string& path) : PrefabPath(path) {}
};

CE_COMPONENT()
struct MaterialComponent {
    CE_PROPERTY() std::string AssetPath;

    // --- VARIABLES D'EXÉCUTION DU SHADER (Non sérialisées ici) ---
    std::shared_ptr<Shader> ShaderInstance = nullptr;
    std::map<int, void*> Textures;
    std::map<std::string, float> FloatOverrides;
    std::map<std::string, glm::vec4> ColorOverrides;
    std::map<std::string, bool> SwitchOverrides;
    std::map<std::string, void*> TextureOverrides;

    MaterialComponent() = default;
    MaterialComponent(const MaterialComponent&) = default;
    MaterialComponent(const std::string& path) { SetAndCompile(path); }

    void SetAndCompile(const std::string& path) {
        AssetPath = path;
        Textures.clear(); FloatOverrides.clear(); ColorOverrides.clear(); TextureOverrides.clear();

        std::ifstream file(path);
        if (!file.is_open()) return;

        nlohmann::json data;
        try { file >> data; } catch(...) { return; }

        std::filesystem::path filePath(path);

        if (filePath.extension() == ".cemat") {
            LoadMaterial(data, filePath);
        } else if (filePath.extension() == ".cematinst") {
            if (data.contains("Parent") && !data["Parent"].get<std::string>().empty()) {
                std::string parentPath = Project::GetProjectDirectory().string() + "/" + data["Parent"].get<std::string>();
                std::ifstream parentFile(parentPath);
                if (parentFile.is_open()) {
                    nlohmann::json parentData;
                    parentFile >> parentData;
                    LoadMaterial(parentData, parentPath);
                }

                if (data.contains("Overrides")) {
                    for (auto& [key, value] : data["Overrides"].items()) {
                        if (value.is_number()) FloatOverrides["u_" + key] = value.get<float>();
                        else if (value.is_array() && value.size() == 4) ColorOverrides["u_" + key] = glm::vec4(value[0], value[1], value[2], value[3]);
                        else if (value.is_string()) {
                            std::string texPath = value.get<std::string>();
                            if (!texPath.empty()) {
                                std::filesystem::path tp(texPath);
                                std::string fullPath = tp.is_absolute() ? tp.string() : (Project::GetProjectDirectory() / tp).string();
                                TextureOverrides["u_" + key] = TextureLoader::LoadTexture(fullPath.c_str());
                            }
                        }
                        else if (value.is_boolean()) SwitchOverrides[key] = value.get<bool>();
                    }
                }
            }
        }
    }

private:
    void LoadMaterial(const nlohmann::json& data, const std::filesystem::path& cematPath) {
        if (data.contains("GeneratedGLSL")) {
            std::string glsl = data["GeneratedGLSL"].get<std::string>();
            std::string defines = "\n";
            if (data.contains("Nodes")) {
                for (auto& node : data["Nodes"]) {
                    if (node["Name"] == "StaticSwitchParameter") {
                        std::string paramName = node.value("ParameterName", "");
                        bool val = node.value("BoolValue", false);
                        if (SwitchOverrides.contains(paramName)) val = SwitchOverrides[paramName];
                        if (val && !paramName.empty()) defines += "#define " + paramName + "\n";
                    }
                }
            }

            size_t pos = glsl.find("#version");
            if (pos != std::string::npos) pos = glsl.find('\n', pos) + 1;
            else pos = 0;
            glsl.insert(pos, defines);

            std::filesystem::path cacheDir = Project::GetCacheDirectory();
            if (!std::filesystem::exists(cacheDir)) std::filesystem::create_directories(cacheDir);

            std::string fragName = std::filesystem::path(AssetPath).stem().string() + ".frag";
            std::filesystem::path fragPath = cacheDir / fragName;

            std::ofstream outFrag(fragPath);
            outFrag << glsl;
            outFrag.close();

            // --- SÉCURITÉ VULKAN : On ne crée pas le programme shader OpenGL ---
            if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
                ShaderInstance = std::make_shared<Shader>("shaders/default.vert", fragPath.string().c_str());
            }
        }

        // --- CHARGEMENT PBR (Les vrais liens automatiques du Material Editor !) ---
        auto loadPBRTexture = [&](const std::string& jsonKey, const std::string& targetName) {
            if (data.contains(jsonKey)) {
                std::string p = data[jsonKey].get<std::string>();
                if (!p.empty()) {
                    std::filesystem::path tp(p);
                    std::string fullPath = tp.is_absolute() ? tp.string() : (Project::GetProjectDirectory() / tp).string();
                    TextureOverrides[targetName] = TextureLoader::LoadTexture(fullPath.c_str());
                }
            }
        };

        loadPBRTexture("PBR_Albedo", "u_Albedo");
        loadPBRTexture("PBR_Normal", "u_Normal");
        loadPBRTexture("PBR_Metallic", "u_Metallic");
        loadPBRTexture("PBR_Roughness", "u_Roughness");
        loadPBRTexture("PBR_AO", "u_AO");

        if (data.contains("PBR_ColorVal")) {
            auto arr = data["PBR_ColorVal"];
            ColorOverrides["u_BaseColor"] = glm::vec4(arr[0], arr[1], arr[2], arr[3]);
        }
        if (data.contains("PBR_MetallicVal")) FloatOverrides["u_Metallic"] = data["PBR_MetallicVal"].get<float>();
        if (data.contains("PBR_RoughnessVal")) FloatOverrides["u_Roughness"] = data["PBR_RoughnessVal"].get<float>();
        if (data.contains("PBR_AOVal")) FloatOverrides["u_AO"] = data["PBR_AOVal"].get<float>();

        if (data.contains("Nodes")) {
            for (auto& node : data["Nodes"]) {
                // 1. Chargement des Textures
                if (node["Name"] == "Texture2D" && node.contains("TexturePath")) {
                    std::string texPath = node["TexturePath"].get<std::string>();
                    if (!texPath.empty()) {
                        int nodeID = node["ID"].get<int>();
                        std::filesystem::path tp(texPath);
                        std::string fullPath = tp.is_absolute() ? tp.string() : (Project::GetProjectDirectory() / tp).string();
                        void* tex = TextureLoader::LoadTexture(fullPath.c_str());
                        Textures[nodeID] = tex;

                        // FIX VULKAN : On lie le nom "u_Albedo" à la texture !
                        if (node.contains("IsParameter") && node["IsParameter"].get<bool>()) {
                            std::string pName = node.value("ParameterName", "");
                            if (!pName.empty()) TextureOverrides["u_" + pName] = tex;
                        }
                    }
                }

                // 2. Chargement des valeurs (Float, Color) pour le PBR
                if (node.contains("IsParameter") && node["IsParameter"].get<bool>()) {
                    std::string pName = node.value("ParameterName", "");
                    if (!pName.empty()) {
                        if (node["Name"] == "Float" && node.contains("FloatValue")) {
                            FloatOverrides["u_" + pName] = node["FloatValue"].get<float>();
                        }
                        else if (node["Name"] == "Color" && node.contains("ColorValue")) {
                            auto arr = node["ColorValue"];
                            ColorOverrides["u_" + pName] = glm::vec4(arr[0], arr[1], arr[2], arr[3]);
                        }
                    }
                }
            }
        }
    }
};

CE_COMPONENT()
struct SkyboxComponent {
    CE_PROPERTY() std::string HDRPath = "";

    // NOUVEAU : Réglages d'exposition et de rotation
    CE_PROPERTY() float Intensity = 0.5f;
    CE_PROPERTY() float Rotation = 0.0f; // En degrés

    // NOUVEAU : Paramètres pour le Sky Atmosphere (Scattering)
    CE_PROPERTY() float PlanetRadius = 6360000.0f; // Terre = 6360km
    CE_PROPERTY() float AtmosphereRadius = 6420000.0f; // Atmosphère = 6420km
    CE_PROPERTY() glm::vec3 RayleighScattering = glm::vec3(5.5e-6f, 13.0e-6f, 22.4e-6f);
    CE_PROPERTY() float MieScattering = 21.0e-6f;
    CE_PROPERTY() float RayleighScaleHeight = 8000.0f;
    CE_PROPERTY() float MieScaleHeight = 1200.0f;
    CE_PROPERTY() float MiePreferredDirection = 0.758f;
    CE_PROPERTY() float SunIntensity = 20.0f;
};

#include "Components.generated.h"