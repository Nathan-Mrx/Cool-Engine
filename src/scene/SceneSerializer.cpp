#include "SceneSerializer.h"
#include "../ecs/Components.h"
#include "../renderer/ModelLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

void SceneSerializer::Serialize(entt::registry& registry, const std::filesystem::path& filepath) {
    json sceneJson;
    sceneJson["Entities"] = json::array();

    // On utilise une vue sur TagComponent car chaque entité en possède un dans Cool Engine
    auto view = registry.view<TagComponent>();

    for (auto entityID : view) {
        json entityJson;
        // Identifiant temporaire (EnTT). Dans un vrai moteur AAA, on utiliserait un composant UUID
        entityJson["EntityID"] = (uint32_t)entityID; 

        if (registry.all_of<TagComponent>(entityID)) {
            entityJson["TagComponent"]["Tag"] = registry.get<TagComponent>(entityID).Tag;
        }

        if (registry.all_of<TransformComponent>(entityID)) {
            auto& tc = registry.get<TransformComponent>(entityID);
            entityJson["TransformComponent"]["Position"] = { tc.Location.x, tc.Location.y, tc.Location.z };
            entityJson["TransformComponent"]["Rotation"] = { tc.Rotation.x, tc.Rotation.y, tc.Rotation.z };
            entityJson["TransformComponent"]["Scale"]    = { tc.Scale.x, tc.Scale.y, tc.Scale.z };
        }

        if (registry.all_of<ColorComponent>(entityID)) {
            auto& cc = registry.get<ColorComponent>(entityID);
            entityJson["ColorComponent"]["Color"] = { cc.Color.x, cc.Color.y, cc.Color.z };
        }

        if (registry.all_of<DirectionalLightComponent>(entityID)) {
            auto& dl = registry.get<DirectionalLightComponent>(entityID);
            entityJson["DirectionalLightComponent"]["Color"] = { dl.Color.x, dl.Color.y, dl.Color.z };
            entityJson["DirectionalLightComponent"]["Ambient"] = dl.AmbientIntensity;
            entityJson["DirectionalLightComponent"]["Diffuse"] = dl.DiffuseIntensity;
        }

        if (registry.all_of<MeshComponent>(entityID)) {
            auto& mc = registry.get<MeshComponent>(entityID);
            entityJson["MeshComponent"]["AssetPath"] = mc.AssetPath;
        }

        // On ajoute l'entité au tableau de la scène
        sceneJson["Entities"].push_back(entityJson);
    };

    std::ofstream stream(filepath);
    if (stream.is_open()) {
        stream << sceneJson.dump(4);
    }
}

bool SceneSerializer::Deserialize(entt::registry& registry, const std::filesystem::path& filepath) {
    if (!std::filesystem::exists(filepath)) return false;

    std::ifstream stream(filepath);
    if (!stream.is_open()) return false;

    json sceneJson;
    try {
        sceneJson = json::parse(stream);
    } catch (json::parse_error& e) {
        std::cerr << "Scene parse error: " << e.what() << std::endl;
        return false;
    }

    // On vide la scène actuelle avant de charger
    registry.clear();

    if (sceneJson.contains("Entities")) {
        for (auto& entityJson : sceneJson["Entities"]) {
            auto entity = registry.create();

            if (entityJson.contains("TagComponent")) {
                registry.emplace<TagComponent>(entity, entityJson["TagComponent"]["Tag"].get<std::string>());
            }

            if (entityJson.contains("TransformComponent")) {
                auto& tc = registry.emplace<TransformComponent>(entity);
                tc.Location = { entityJson["TransformComponent"]["Position"][0], entityJson["TransformComponent"]["Position"][1], entityJson["TransformComponent"]["Position"][2] };
                tc.Rotation = { entityJson["TransformComponent"]["Rotation"][0], entityJson["TransformComponent"]["Rotation"][1], entityJson["TransformComponent"]["Rotation"][2] };
                tc.Scale    = { entityJson["TransformComponent"]["Scale"][0], entityJson["TransformComponent"]["Scale"][1], entityJson["TransformComponent"]["Scale"][2] };
            }

            if (entityJson.contains("ColorComponent")) {
                auto& cc = registry.emplace<ColorComponent>(entity);
                cc.Color = { entityJson["ColorComponent"]["Color"][0], entityJson["ColorComponent"]["Color"][1], entityJson["ColorComponent"]["Color"][2] };
            }

            if (entityJson.contains("DirectionalLightComponent")) {
                auto& dl = registry.emplace<DirectionalLightComponent>(entity);
                dl.Color = { entityJson["DirectionalLightComponent"]["Color"][0], entityJson["DirectionalLightComponent"]["Color"][1], entityJson["DirectionalLightComponent"]["Color"][2] };
                dl.AmbientIntensity = entityJson["DirectionalLightComponent"]["Ambient"].get<float>();
                dl.DiffuseIntensity = entityJson["DirectionalLightComponent"]["Diffuse"].get<float>();
            }

            if (entityJson.contains("MeshComponent")) {
                auto& mc = registry.emplace<MeshComponent>(entity);
                mc.AssetPath = entityJson["MeshComponent"]["AssetPath"].get<std::string>();
                if (!mc.AssetPath.empty()) {
                    mc.MeshData = ModelLoader::LoadModel(mc.AssetPath);
                }
            }
        }
    }
    return true;
}