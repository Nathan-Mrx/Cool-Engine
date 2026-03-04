#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include "../ecs/Components.h"

class ScriptRegistry {
public:
    static inline std::unordered_map<std::string, std::function<void(NativeScriptComponent&)>> Registry;

    // Cette fonction sera appelée automatiquement par notre macro !
    static void Register(const std::string& name, std::function<void(NativeScriptComponent&)> func) {
        Registry[name] = func;
    }
};


#define REGISTER_SCRIPT(ClassName) \
namespace { \
struct ClassName##_Register { \
ClassName##_Register() { \
ScriptRegistry::Register(#ClassName, [](NativeScriptComponent& nsc) { nsc.Bind<ClassName>(); }); \
} \
}; \
static ClassName##_Register global_##ClassName##_Register_Instance; \
}