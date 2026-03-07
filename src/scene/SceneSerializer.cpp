#include "SceneSerializer.h"
#include "../ecs/Components.h"
#include "Entity.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <unordered_map> // Indispensable pour notre mapping d'UUID !

#include "renderer/PrimitiveFactory.h"
#include "scripts/ScriptRegistry.h"

using json = nlohmann::json;

#include <tuple>

// On suppose que tu as accès à GetComponentName<T>() comme dans ton Inspector
template<typename T>
void SerializeSingleComponent(nlohmann::json& entityJson, Entity entity) {
    if (entity.HasComponent<T>()) {
        const char* name = GetComponentName<T>();
        // nlohmann::json va automatiquement appeler la fonction to_json() de T
        entityJson[name] = entity.GetComponent<T>();
    }
}

template<typename T>
void DeserializeSingleComponent(const nlohmann::json& entityJson, Entity entity) {
    const char* name = GetComponentName<T>();
    if (entityJson.contains(name)) {
        // nlohmann::json va automatiquement appeler la fonction from_json() pour recréer T
        auto& comp = entity.AddComponent<T>();
        comp = entityJson[name].get<T>();
    }
}

SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
    : m_Scene(scene) {}

void SceneSerializer::Serialize(const std::string& filepath) {
    json sceneData;
    sceneData["Scene"] = "Untitled";
    json entitiesData = json::array();

    // 1. On crée une vue sur toutes les entités
    auto view = m_Scene->m_Registry.view<TagComponent>();

    for (auto entityID : view) {
        Entity entity = { entityID, m_Scene.get() };
        if (!entity) continue;

        json entityJson;

        // --- IDENTITÉ (UUID) ---
        if (entity.HasComponent<IDComponent>()) {
            entityJson["Entity"] = (uint64_t)entity.GetComponent<IDComponent>().ID;
        }

        // --- COMPOSANTS STANDARDS ---
        if (entity.HasComponent<TagComponent>()) {
            entityJson["TagComponent"]["Tag"] = entity.GetComponent<TagComponent>().Tag;
        }

        if (entity.HasComponent<TransformComponent>()) {
            auto& tc = entity.GetComponent<TransformComponent>();
            entityJson["TransformComponent"]["Location"] = { tc.Location.x, tc.Location.y, tc.Location.z };
            entityJson["TransformComponent"]["Rotation"] = { tc.RotationEuler.x, tc.RotationEuler.y, tc.RotationEuler.z };
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

        if (entity.HasComponent<RigidBodyComponent>()) {
            auto& rb = entity.GetComponent<RigidBodyComponent>();
            entityJson["RigidBodyComponent"]["Type"] = (int)rb.Type;
            entityJson["RigidBodyComponent"]["Mass"] = rb.Mass;
        }

        if (entity.HasComponent<BoxColliderComponent>()) {
            auto& bc = entity.GetComponent<BoxColliderComponent>();
            entityJson["BoxColliderComponent"]["HalfSize"] = { bc.HalfSize.x, bc.HalfSize.y, bc.HalfSize.z };
            entityJson["BoxColliderComponent"]["Offset"] = { bc.Offset.x, bc.Offset.y, bc.Offset.z };
            entityJson["BoxColliderComponent"]["Friction"] = bc.Friction;
            entityJson["BoxColliderComponent"]["Restitution"] = bc.Restitution;
        }

        if (entity.HasComponent<CameraComponent>()) {
            auto& camera = entity.GetComponent<CameraComponent>();
            entityJson["CameraComponent"]["Primary"] = camera.Primary;
            entityJson["CameraComponent"]["FOV"] = camera.FOV;
            entityJson["CameraComponent"]["NearClip"] = camera.NearClip;
            entityJson["CameraComponent"]["FarClip"] = camera.FarClip;
        }

        if (entity.HasComponent<NativeScriptComponent>()) {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            entityJson["NativeScriptComponent"]["ScriptName"] = nsc.ScriptName;
        }

        // --- HIERARCHIE (RELATIONSHIP) ---
        if (entity.HasComponent<RelationshipComponent>()) {
            auto& rel = entity.GetComponent<RelationshipComponent>();

            // Fonction lambda pour extraire l'UUID d'un ID EnTT
            auto getUUID = [&](entt::entity id) -> uint64_t {
                if (id == entt::null) return 0;
                Entity e{ id, m_Scene.get() };
                if (e.HasComponent<IDComponent>()) return (uint64_t)e.GetComponent<IDComponent>().ID;
                return 0;
            };

            entityJson["RelationshipComponent"]["Parent"] = getUUID(rel.Parent);
            entityJson["RelationshipComponent"]["FirstChild"] = getUUID(rel.FirstChild);
            entityJson["RelationshipComponent"]["PreviousSibling"] = getUUID(rel.PreviousSibling);
            entityJson["RelationshipComponent"]["NextSibling"] = getUUID(rel.NextSibling);
        }

        if (entity.HasComponent<PrefabComponent>()) {
            entityJson["PrefabComponent"]["PrefabPath"] = entity.GetComponent<PrefabComponent>().PrefabPath;
        }

        if (entity.HasComponent<MaterialComponent>()) {
            entityJson["MaterialComponent"]["AssetPath"] = entity.GetComponent<MaterialComponent>().AssetPath;
        }
        if (entity.HasComponent<SkyboxComponent>())
        {
            entityJson["SkyboxComponent"]["HDRPath"] = entity.GetComponent<SkyboxComponent>().HDRPath;
            entityJson["SkyboxComponent"]["Intensity"] = entity.GetComponent<SkyboxComponent>().Intensity;
            entityJson["SkyboxComponent"]["Rotation"] = entity.GetComponent<SkyboxComponent>().Rotation;
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
    if (entities.is_null()) return true;

    // Dictionnaire pour relier les UUID sauvegardés aux nouveaux ID en mémoire
    std::unordered_map<uint64_t, entt::entity> uuidToEntityMap;

    // ==========================================
    // PASSE 1 : Instanciation et chargement des données
    // ==========================================
    for (auto& entityJson : entities) {

        // 1. Extraction de l'UUID et du Nom
        uint64_t uuid = entityJson["Entity"].get<uint64_t>();
        std::string name = "Empty Entity";
        if (entityJson.contains("TagComponent")) {
            name = entityJson["TagComponent"]["Tag"].get<std::string>();
        }

        // 2. Création de l'entité avec son véritable UUID
        Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);

        // On mémorise son nouvel ID EnTT pour la passe 2
        uuidToEntityMap[uuid] = deserializedEntity;

        // 3. Chargement des composants classiques
        if (entityJson.contains("TransformComponent")) {
            auto& tc = deserializedEntity.GetComponent<TransformComponent>();
            auto& jTc = entityJson["TransformComponent"];

            if (jTc.contains("Location")) {
                tc.Location = { jTc["Location"][0], jTc["Location"][1], jTc["Location"][2] };
            }
            if (jTc.contains("Rotation")) {
                glm::vec3 eulerRotation = { jTc["Rotation"][0], jTc["Rotation"][1], jTc["Rotation"][2] };
                tc.RotationEuler = eulerRotation;
                tc.Rotation = glm::quat(glm::radians(eulerRotation));
            }
            if (jTc.contains("Scale")) {
                tc.Scale = { jTc["Scale"][0], jTc["Scale"][1], jTc["Scale"][2] };
            }
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
                if (mc.AssetPath == "Primitive::Cube") {
                    mc.MeshData = PrimitiveFactory::CreateCube();
                } else if (mc.AssetPath == "Primitive::Sphere") {
                    mc.MeshData = PrimitiveFactory::CreateSphere();
                } else if (mc.AssetPath == "Primitive::Plane") {
                    mc.MeshData = PrimitiveFactory::CreatePlane();
                } else {
                    mc.MeshData = ModelLoader::LoadModel(mc.AssetPath);
                }
            }
        }

        if (entityJson.contains("RigidBodyComponent")) {
            auto& jRb = entityJson["RigidBodyComponent"];
            if (!deserializedEntity.HasComponent<RigidBodyComponent>()) deserializedEntity.AddComponent<RigidBodyComponent>();
            auto& rb = deserializedEntity.GetComponent<RigidBodyComponent>();

            rb.Type = (RigidBodyType)jRb["Type"].get<int>();
            rb.Mass = jRb["Mass"].get<float>();
        }

        if (entityJson.contains("BoxColliderComponent")) {
            auto& jBc = entityJson["BoxColliderComponent"];
            if (!deserializedEntity.HasComponent<BoxColliderComponent>()) deserializedEntity.AddComponent<BoxColliderComponent>();
            auto& bc = deserializedEntity.GetComponent<BoxColliderComponent>();

            bc.HalfSize = { jBc["HalfSize"][0], jBc["HalfSize"][1], jBc["HalfSize"][2] };
            bc.Offset   = { jBc["Offset"][0], jBc["Offset"][1], jBc["Offset"][2] };
            bc.Friction    = jBc["Friction"].get<float>();
            bc.Restitution = jBc["Restitution"].get<float>();
        }

        if (entityJson.contains("CameraComponent")) {
            auto& jCamera = entityJson["CameraComponent"];
            if (!deserializedEntity.HasComponent<CameraComponent>()) deserializedEntity.AddComponent<CameraComponent>();
            auto& camera = deserializedEntity.GetComponent<CameraComponent>();

            camera.Primary = jCamera["Primary"].get<bool>();
            camera.FOV = jCamera["FOV"].get<float>();
            camera.NearClip = jCamera["NearClip"].get<float>();
            camera.FarClip = jCamera["FarClip"].get<float>();
        }

        if (entityJson.contains("NativeScriptComponent")) {
            auto scriptName = entityJson["NativeScriptComponent"]["ScriptName"].get<std::string>();

            if (ScriptRegistry::Registry.find(scriptName) != ScriptRegistry::Registry.end()) {
                auto& nsc = deserializedEntity.AddComponent<NativeScriptComponent>();
                nsc.ScriptName = scriptName;
                ScriptRegistry::Registry[scriptName](nsc);
            } else {
                std::cout << "[Serializer] Warning: Script '" << scriptName << "' not found in Registry." << std::endl;
            }
        }

        if (entityJson.contains("PrefabComponent")) {
            auto& pc = deserializedEntity.AddComponent<PrefabComponent>();
            pc.PrefabPath = entityJson["PrefabComponent"]["PrefabPath"].get<std::string>();
        }

        if (entityJson.contains("MaterialComponent")) {
            auto& jMat = entityJson["MaterialComponent"];
            if (!deserializedEntity.HasComponent<MaterialComponent>()) deserializedEntity.AddComponent<MaterialComponent>();
            auto& mat = deserializedEntity.GetComponent<MaterialComponent>();

            // Appeler SetAndCompile va lire le fichier et recréer le Shader !
            mat.SetAndCompile(jMat["AssetPath"].get<std::string>());
        }

        if (entityJson.contains("SkyboxComponent"))
        {
            auto& jSkybox = entityJson["SkyboxComponent"];
            if (!deserializedEntity.HasComponent<SkyboxComponent>()) deserializedEntity.AddComponent<SkyboxComponent>();
            auto& skybox = deserializedEntity.GetComponent<SkyboxComponent>();

            skybox.HDRPath = jSkybox["HDRPath"].get<std::string>();
            skybox.Intensity = jSkybox["Intensity"].get<float>();
            skybox.Rotation = jSkybox["Rotation"].get<float>();
        }

    }

    // ==========================================
    // PASSE 2 : Reconstruction de la hiérarchie (SCÈNES NORMALES)
    // ==========================================
    for (auto& entityJson : entities) {
        if (entityJson.contains("RelationshipComponent")) {
            uint64_t entityUUID = entityJson["Entity"].get<uint64_t>();
            Entity deserializedEntity{ uuidToEntityMap[entityUUID], m_Scene.get() };

            auto& rel = deserializedEntity.AddComponent<RelationshipComponent>();

            auto getEntity = [&](uint64_t id) -> entt::entity {
                if (id == 0 || uuidToEntityMap.find(id) == uuidToEntityMap.end()) return entt::null;
                return uuidToEntityMap[id];
            };

            rel.Parent = getEntity(entityJson["RelationshipComponent"]["Parent"].get<uint64_t>());
            rel.FirstChild = getEntity(entityJson["RelationshipComponent"]["FirstChild"].get<uint64_t>());
            rel.PreviousSibling = getEntity(entityJson["RelationshipComponent"]["PreviousSibling"].get<uint64_t>());
            rel.NextSibling = getEntity(entityJson["RelationshipComponent"]["NextSibling"].get<uint64_t>());
        }
    }

    return true;
}

Entity SceneSerializer::DeserializePrefab(const std::string& filepath) {
    std::ifstream stream(filepath);
    if (!stream.is_open()) return {};

    json sceneData;
    try { stream >> sceneData; }
    catch (json::parse_error& e) { return {}; }

    auto entities = sceneData["Entities"];
    if (entities.is_null() || entities.empty()) return {};

    std::unordered_map<uint64_t, entt::entity> oldUuidToNewEntityMap;
    Entity rootEntity = {}; // Mémorisera la racine du prefab

    // ==========================================
    // PASSE 1 : Création avec de NOUVEAUX UUIDs
    // ==========================================
    for (auto& entityJson : entities) {
        uint64_t oldUuid = entityJson["Entity"].get<uint64_t>();
        std::string name = entityJson.contains("TagComponent") ? entityJson["TagComponent"]["Tag"].get<std::string>() : "Prefab Part";

        // MAGIE : CreateEntity génère un NOUVEL UUID automatiquement !
        Entity newEntity = m_Scene->CreateEntity(name);
        oldUuidToNewEntityMap[oldUuid] = newEntity;

        // --- RECOPIE DES COMPOSANTS (Copier-coller de ton Deserialize) ---
        if (entityJson.contains("TransformComponent")) {
            auto& tc = newEntity.GetComponent<TransformComponent>();
            auto& jTc = entityJson["TransformComponent"];
            if (jTc.contains("Location")) tc.Location = { jTc["Location"][0], jTc["Location"][1], jTc["Location"][2] };
            if (jTc.contains("Rotation")) {
                tc.RotationEuler = { jTc["Rotation"][0], jTc["Rotation"][1], jTc["Rotation"][2] };
                tc.Rotation = glm::quat(glm::radians(tc.RotationEuler));
            }
            if (jTc.contains("Scale")) tc.Scale = { jTc["Scale"][0], jTc["Scale"][1], jTc["Scale"][2] };
        }

        if (entityJson.contains("ColorComponent")) {
            auto& cc = newEntity.AddComponent<ColorComponent>();
            cc.Color = { entityJson["ColorComponent"]["Color"][0], entityJson["ColorComponent"]["Color"][1], entityJson["ColorComponent"]["Color"][2] };
        }

        if (entityJson.contains("MeshComponent")) {
            auto& mc = newEntity.AddComponent<MeshComponent>();
            mc.AssetPath = entityJson["MeshComponent"]["AssetPath"].get<std::string>();
            if (mc.AssetPath == "Primitive::Cube") mc.MeshData = PrimitiveFactory::CreateCube();
            else if (mc.AssetPath == "Primitive::Sphere") mc.MeshData = PrimitiveFactory::CreateSphere();
            else if (mc.AssetPath == "Primitive::Plane") mc.MeshData = PrimitiveFactory::CreatePlane();
            else if (!mc.AssetPath.empty()) mc.MeshData = ModelLoader::LoadModel(mc.AssetPath);
        }

        if (entityJson.contains("DirectionalLightComponent")) {
            auto& dlc = newEntity.AddComponent<DirectionalLightComponent>();
            dlc.Color = { entityJson["DirectionalLightComponent"]["Color"][0], entityJson["DirectionalLightComponent"]["Color"][1], entityJson["DirectionalLightComponent"]["Color"][2] };
            dlc.AmbientIntensity = entityJson["DirectionalLightComponent"]["Ambient"].get<float>();
            dlc.DiffuseIntensity = entityJson["DirectionalLightComponent"]["Diffuse"].get<float>();
        }

        if (entityJson.contains("RigidBodyComponent")) {
            auto& rb = newEntity.AddComponent<RigidBodyComponent>();
            rb.Type = (RigidBodyType)entityJson["RigidBodyComponent"]["Type"].get<int>();
            rb.Mass = entityJson["RigidBodyComponent"]["Mass"].get<float>();
        }

        if (entityJson.contains("BoxColliderComponent")) {
            auto& bc = newEntity.AddComponent<BoxColliderComponent>();
            bc.HalfSize = { entityJson["BoxColliderComponent"]["HalfSize"][0], entityJson["BoxColliderComponent"]["HalfSize"][1], entityJson["BoxColliderComponent"]["HalfSize"][2] };
            bc.Offset = { entityJson["BoxColliderComponent"]["Offset"][0], entityJson["BoxColliderComponent"]["Offset"][1], entityJson["BoxColliderComponent"]["Offset"][2] };
        }

        if (entityJson.contains("CameraComponent")) {
            auto& cam = newEntity.AddComponent<CameraComponent>();
            cam.Primary = entityJson["CameraComponent"]["Primary"].get<bool>();
            cam.FOV = entityJson["CameraComponent"]["FOV"].get<float>();
        }

        if (entityJson.contains("NativeScriptComponent")) {
            auto scriptName = entityJson["NativeScriptComponent"]["ScriptName"].get<std::string>();
            if (ScriptRegistry::Registry.find(scriptName) != ScriptRegistry::Registry.end()) {
                auto& nsc = newEntity.AddComponent<NativeScriptComponent>();
                nsc.ScriptName = scriptName;
                ScriptRegistry::Registry[scriptName](nsc);
            }
        }

        if (entityJson.contains("MaterialComponent")) {
            auto& mat = newEntity.AddComponent<MaterialComponent>();
            mat.SetAndCompile(entityJson["MaterialComponent"]["AssetPath"].get<std::string>());
        }
    }

    // ==========================================
    // PASSE 2 : Traduction de la hiérarchie interne
    // ==========================================
    for (auto& entityJson : entities) {
        uint64_t oldUuid = entityJson["Entity"].get<uint64_t>();
        Entity newEntity{ oldUuidToNewEntityMap[oldUuid], m_Scene.get() };

        if (entityJson.contains("RelationshipComponent")) {
            auto& rel = newEntity.AddComponent<RelationshipComponent>();

            auto getNewEntity = [&](uint64_t id) -> entt::entity {
                if (id == 0 || oldUuidToNewEntityMap.find(id) == oldUuidToNewEntityMap.end()) return entt::null;
                return oldUuidToNewEntityMap[id];
            };

            rel.Parent = getNewEntity(entityJson["RelationshipComponent"]["Parent"].get<uint64_t>());
            rel.FirstChild = getNewEntity(entityJson["RelationshipComponent"]["FirstChild"].get<uint64_t>());
            rel.PreviousSibling = getNewEntity(entityJson["RelationshipComponent"]["PreviousSibling"].get<uint64_t>());
            rel.NextSibling = getNewEntity(entityJson["RelationshipComponent"]["NextSibling"].get<uint64_t>());

            // Si l'entité n'a pas de parent dans le JSON, c'est la racine absolue du Prefab !
            if (rel.Parent == entt::null) {
                rootEntity = newEntity;
            }
        } else {
            // S'il n'y a pas de composant hiérarchie du tout, c'est une racine implicite
            if (!rootEntity) rootEntity = newEntity;
        }
    }

    if (rootEntity) {
        rootEntity.AddComponent<PrefabComponent>(filepath);
    }

    return rootEntity;
}

nlohmann::json SceneSerializer::SerializeEntity(Entity entity) {
    nlohmann::json entityJson;
    if (!entity) return entityJson;

    entityJson["Entity"] = (uint64_t)entity.GetUUID();

    // LA MAGIE C++17 : Le compilateur génère tous les if automatiquement !
    std::apply([&](auto... args) {
        (SerializeSingleComponent<decltype(args)>(entityJson, entity), ...);
    }, AllComponents{});

    return entityJson;
}

Entity SceneSerializer::DeserializeEntity(const nlohmann::json& entityJson) {
    uint64_t uuid = entityJson["Entity"].get<uint64_t>();

    // Astuce pour récupérer le nom si le TagComponent existe dans le JSON
    std::string name = "Empty Entity";
    if (entityJson.contains("TagComponent")) name = entityJson["TagComponent"]["Tag"].get<std::string>();

    Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);

    // LA MAGIE INVERSE
    std::apply([&](auto... args) {
        (DeserializeSingleComponent<decltype(args)>(entityJson, deserializedEntity), ...);
    }, AllComponents{});

    return deserializedEntity;
}