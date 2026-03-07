#pragma once
#include "Scene.h"
#include <memory>
#include <string>
#include <nlohmann/json_fwd.hpp>

class SceneSerializer {
public:
    SceneSerializer(const std::shared_ptr<Scene>& scene);

    void Serialize(const std::string& filepath);
    bool Deserialize(const std::string& filepath);

    Entity DeserializePrefab(const std::string& filepath);

    nlohmann::json SerializeEntity(Entity entity);
    Entity DeserializeEntity(const nlohmann::json& entityJson);

private:
    std::shared_ptr<Scene> m_Scene;
};
