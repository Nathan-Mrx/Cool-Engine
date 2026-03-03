#include "SceneSerializer.h"
#include "../ecs/Components.h"
#include "Entity.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

#include "renderer/PrimitiveFactory.h"

using json = nlohmann::json;

SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
    : m_Scene(scene) {}

void SceneSerializer::Serialize(const std::string& filepath) {
    json sceneData;
    sceneData["Scene"] = "Untitled";
    json entitiesData = json::array();

    // 1. On crée une vue sur toutes les entités qui ont un nom (donc toutes les entités valides)
    auto view = m_Scene->m_Registry.view<TagComponent>();

    // 2. On itère avec une boucle for classique
    for (auto entityID : view) {
        Entity entity = { entityID, m_Scene.get() };

        // 3. IMPORTANT : on utilise 'continue' au lieu de 'return' dans une boucle
        if (!entity) continue;

        json entityJson;
        entityJson["EntityID"] = (uint32_t)entityID;

        if (entity.HasComponent<TagComponent>()) {
            entityJson["TagComponent"]["Tag"] = entity.GetComponent<TagComponent>().Tag;
        }

        if (entity.HasComponent<TransformComponent>()) {
            auto& tc = entity.GetComponent<TransformComponent>();
            entityJson["TransformComponent"]["Location"] = { tc.Location.x, tc.Location.y, tc.Location.z };
            entityJson["TransformComponent"]["Rotation"] = { tc.Rotation.x, tc.Rotation.y, tc.Rotation.z };
            entityJson["TransformComponent"]["Scale"]    = { tc.Scale.x, tc.Scale.y, tc.Scale.z };
        }

        if (entity.HasComponent<ColorComponent>()) {
            auto& cc = entity.GetComponent<ColorComponent>();
            entityJson["ColorComponent"]["Color"] = { cc.Color.x, cc.Color.y, cc.Color.z };
        }

        if (entity.HasComponent<DirectionalLightComponent>()) {
            auto& dlc = entity.GetComponent<DirectionalLightComponent>();
            entityJson["DirectionalLightComponent"]["Color"] = { dlc.Color.x, dlc.Color.y, dlc.Color.z };
            entityJson["DirectionalLightComponent"]["Ambient"] = dlc.AmbientIntensity;
            entityJson["DirectionalLightComponent"]["Diffuse"] = dlc.DiffuseIntensity;
        }

        if (entity.HasComponent<MeshComponent>()) {
            auto& mc = entity.GetComponent<MeshComponent>();
            entityJson["MeshComponent"]["AssetPath"] = mc.AssetPath;
        }

        entitiesData.push_back(entityJson);
    }

    sceneData["Entities"] = entitiesData;

    std::ofstream fout(filepath);
    fout << sceneData.dump(4);
}

bool SceneSerializer::Deserialize(const std::string& filepath) {
    std::ifstream stream(filepath);
    if (!stream.is_open()) return false;

    json sceneData;
    try {
        stream >> sceneData;
    } catch (json::parse_error& e) {
        std::cerr << "Erreur de parsing JSON : " << e.what() << '\n';
        return false;
    }

    auto entities = sceneData["Entities"];
    if (entities.is_null()) return true; // Scène vide

    for (auto& entityJson : entities) {
        std::string name = "Empty Entity";
        if (entityJson.contains("TagComponent")) {
            name = entityJson["TagComponent"]["Tag"].get<std::string>();
        }

        // Création de l'entité (qui ajoute souvent un Tag et un Transform par défaut)
        Entity deserializedEntity = m_Scene->CreateEntity(name);

        if (entityJson.contains("TransformComponent")) {
            auto& tc = deserializedEntity.GetComponent<TransformComponent>(); // Déjà ajouté par CreateEntity
            auto& jTc = entityJson["TransformComponent"];
            tc.Location = { jTc["Location"][0], jTc["Location"][1], jTc["Location"][2] };
            tc.Rotation = { jTc["Rotation"][0], jTc["Rotation"][1], jTc["Rotation"][2] };
            tc.Scale    = { jTc["Scale"][0], jTc["Scale"][1], jTc["Scale"][2] };
        }

        if (entityJson.contains("ColorComponent")) {
            auto& jCc = entityJson["ColorComponent"];
            if (!deserializedEntity.HasComponent<ColorComponent>()) deserializedEntity.AddComponent<ColorComponent>();
            auto& cc = deserializedEntity.GetComponent<ColorComponent>();
            cc.Color = { jCc["Color"][0], jCc["Color"][1], jCc["Color"][2] };
        }

        if (entityJson.contains("DirectionalLightComponent")) {
            auto& jDlc = entityJson["DirectionalLightComponent"];
            if (!deserializedEntity.HasComponent<DirectionalLightComponent>()) deserializedEntity.AddComponent<DirectionalLightComponent>();
            auto& dlc = deserializedEntity.GetComponent<DirectionalLightComponent>();
            dlc.Color = { jDlc["Color"][0], jDlc["Color"][1], jDlc["Color"][2] };
            dlc.AmbientIntensity = jDlc["Ambient"].get<float>();
            dlc.DiffuseIntensity = jDlc["Diffuse"].get<float>();
        }

        if (entityJson.contains("MeshComponent")) {
            auto& jMc = entityJson["MeshComponent"];
            if (!deserializedEntity.HasComponent<MeshComponent>()) deserializedEntity.AddComponent<MeshComponent>();
            auto& mc = deserializedEntity.GetComponent<MeshComponent>();

            mc.AssetPath = jMc["AssetPath"].get<std::string>();
            if (!mc.AssetPath.empty()) {
                // --- ROUTAGE DES ASSETS VS PRIMITIVES ---
                if (mc.AssetPath == "Primitive::Cube") {
                    mc.MeshData = PrimitiveFactory::CreateCube();
                } else if (mc.AssetPath == "Primitive::Sphere") {
                    mc.MeshData = PrimitiveFactory::CreateSphere();
                } else if (mc.AssetPath == "Primitive::Plane") {
                    mc.MeshData = PrimitiveFactory::CreatePlane();
                } else {
                    // C'est un vrai fichier .obj/.fbx
                    mc.MeshData = ModelLoader::LoadModel(mc.AssetPath);
                }
            }
        }
    }
    return true;
}