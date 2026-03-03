#pragma once
#include <entt/entt.hpp>
#include <filesystem>

class SceneSerializer {
public:
    static void Serialize(entt::registry& registry, const std::filesystem::path& filepath);
    static bool Deserialize(entt::registry& registry, const std::filesystem::path& filepath);
};