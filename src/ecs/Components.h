#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <string>
#include <tuple>
#include <filesystem>

#include "renderer/Mesh.h"
#include <nfd.hpp>
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

// --- STRUCTURES DE DONNÉES DE BASE ---

struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vector3() = default;
    Vector3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    glm::vec3 ToGlm() const { return { x, y, z }; }
    float* Data() { return &x; }
};

// --- COMPOSANTS ECS ---

struct TagComponent {
    std::string Tag;

    TagComponent() = default;
    TagComponent(const std::string& tag) : Tag(tag) {}

    void OnImGuiRender() {
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, Tag.c_str(), sizeof(buffer));
        if (ImGui::InputText("Entity Name", buffer, sizeof(buffer))) {
            Tag = std::string(buffer);
        }
    }
};

struct TransformComponent {
    glm::vec3 Location = { 0.0f, 0.0f, 0.0f };
    glm::vec3 RotationEuler = { 0.0f, 0.0f, 0.0f }; // <-- LE CACHE POUR L'UI
    glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // <-- POUR LES MATHS ET LA PHYSIQUE
    glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

    TransformComponent() = default;
    TransformComponent(const TransformComponent&) = default;
    TransformComponent(const glm::vec3& translation) : Location(translation) {}

    glm::mat4 GetTransform() const {
        return glm::translate(glm::mat4(1.0f), Location)
             * glm::toMat4(Rotation)
             * glm::scale(glm::mat4(1.0f), Scale);
    }

    glm::vec3 GetForwardVector() const {
        // Comme tu es en Left-Handed (perspectiveLH), "l'avant" est généralement +Z.
        // On prend un vecteur qui pointe vers l'avant, et on lui applique notre Quaternion !
        return glm::rotate(Rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    glm::vec3 GetUpVector() const {
        return glm::rotate(Rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    glm::vec3 GetRightVector() const {
        return glm::rotate(Rotation, glm::vec3(1.0f, 0.0f, 0.0f));
    }
};

struct ColorComponent {
    glm::vec3 Color = { 1.0f, 1.0f, 1.0f };

    ColorComponent() = default;
    ColorComponent(const glm::vec3& color) : Color(color) {}
};

struct CameraComponent {
    bool Primary = true; // Si vrai, c'est cette caméra qui est rendue à l'écran
    float FOV = 90.0f;   // Champ de vision (en degrés)
    float NearClip = 0.1f;
    float FarClip = 100000.0f;

    CameraComponent() = default;
    CameraComponent(const CameraComponent&) = default;

    void OnImGuiRender() {
        ImGui::Checkbox("Primary Camera", &Primary);
        ImGui::DragFloat("FOV", &FOV, 0.5f, 10.0f, 150.0f);
        ImGui::DragFloat("Near Clip", &NearClip, 0.1f, 0.01f, 100.0f);
        ImGui::DragFloat("Far Clip", &FarClip, 100.0f, 100.0f, 1000000.0f);
    }
};

struct MeshComponent {
    std::shared_ptr<Mesh> MeshData;
    std::string AssetPath;

    void OnImGuiRender() {
        if (MeshData) {
            ImGui::TextWrapped("Path: %s", AssetPath.c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "No Mesh Assigned");
        }

        ImGui::Spacing();

        // On crée une zone visuelle pour le Drag & Drop
        ImGui::Button("Drop .obj Here to Load", ImVec2(-1, 40));

        // --- NOUVEAU : DRAG & DROP TARGET (Inspector) ---
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* path = (const char*)payload->Data;
                std::filesystem::path filepath = path;

                if (filepath.extension() == ".obj" || filepath.extension() == ".fbx") {
                    AssetPath = filepath.string();
                    MeshData = ModelLoader::LoadModel(AssetPath);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Garde l'ancien bouton NFD pour la rétrocompatibilité si tu veux chercher en dehors du projet
        if (ImGui::Button("Or Browse...", ImVec2(-1, 0))) {
            nfdchar_t* outPath = nullptr;
            if (NFD::OpenDialog(outPath, nullptr, 0, nullptr) == NFD_OKAY) {
                AssetPath = outPath;
                MeshData = ModelLoader::LoadModel(AssetPath);
                NFD::FreePath(outPath);
            }
        }
    }
};

struct DirectionalLightComponent {
    glm::vec3 Color = { 1.0f, 1.0f, 1.0f }; // Lumière blanche par défaut
    float AmbientIntensity = 0.2f;          // Lumière ambiante (pour ne pas avoir de noir total)
    float DiffuseIntensity = 0.8f;          // Puissance de la lumière directe

    void OnImGuiRender() {
        ImGui::ColorEdit3("Light Color", &Color[0]);
        ImGui::DragFloat("Ambient", &AmbientIntensity, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Diffuse", &DiffuseIntensity, 0.01f, 0.0f, 1.0f);
    }
};

// Définit le comportement de l'objet dans le monde physique
enum class RigidBodyType { Static = 0, Kinematic, Dynamic };

struct RigidBodyComponent {
    RigidBodyType Type = RigidBodyType::Static;
    float Mass = 1.0f;

    // Identifiant interne utilisé par Jolt (on l'initialise à une valeur invalide)
    uint32_t RuntimeBodyID = 0xFFFFFFFF;

    RigidBodyComponent() = default;
    RigidBodyComponent(const RigidBodyComponent&) = default;

    void OnImGuiRender() {
        const char* bodyTypeStrings[] = { "Static", "Kinematic", "Dynamic" };
        const char* currentBodyTypeString = bodyTypeStrings[(int)Type];

        if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
            for (int i = 0; i < 3; i++) {
                bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                    Type = (RigidBodyType)i;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (Type == RigidBodyType::Dynamic) {
            ImGui::DragFloat("Mass", &Mass, 0.1f, 0.01f, 10000.0f);
        }
    }
};

// Définit la forme de la boîte de collision
struct BoxColliderComponent {
    glm::vec3 HalfSize = { 0.5f, 0.5f, 0.5f }; // La taille de la boîte (depuis le centre)
    glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };   // Décalage par rapport au Transform de l'entité

    float Friction = 0.5f;
    float Restitution = 0.0f; // Bounciness (Rebond)

    BoxColliderComponent() = default;
    BoxColliderComponent(const BoxColliderComponent&) = default;

    void OnImGuiRender() {
        // Multiplié par 2 visuellement pour que l'utilisateur rentre la taille totale (plus intuitif)
        glm::vec3 size = HalfSize * 2.0f;
        if (ImGui::DragFloat3("Size", glm::value_ptr(size), 0.1f)) {
            HalfSize = size / 2.0f;
        }

        ImGui::DragFloat3("Offset", glm::value_ptr(Offset), 0.1f);
        ImGui::DragFloat("Friction", &Friction, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Restitution", &Restitution, 0.01f, 0.0f, 1.0f);
    }
};

struct NativeScriptComponent {
    std::string ScriptName = "None";
    ScriptableNode* Instance = nullptr;

    // Pointeurs de fonctions pour allouer et désallouer la mémoire dynamiquement
    ScriptableNode* (*InstantiateScript)() = nullptr;
    void (*DestroyScript)(NativeScriptComponent*) = nullptr;

    // Fonction template magique pour lier un script au composant
    template<typename T>
    void Bind() {
        InstantiateScript = []() { return static_cast<ScriptableNode*>(new T()); };
        DestroyScript = [](NativeScriptComponent* nsc) {
            delete nsc->Instance;
            nsc->Instance = nullptr;
        };
    }

    void OnImGuiRender() {}
};

struct RelationshipComponent {
    entt::entity Parent = entt::null;
    entt::entity FirstChild = entt::null;
    entt::entity PreviousSibling = entt::null;
    entt::entity NextSibling = entt::null;

    RelationshipComponent() = default;
    RelationshipComponent(const RelationshipComponent&) = default;

    void OnImGuiRender() {
        // Pour l'instant, on n'affiche rien dans l'Inspector.
        // La hiérarchie se gérera visuellement dans l'arbre !
    }
};

struct IDComponent {
    UUID ID;

    IDComponent() = default;
    IDComponent(const IDComponent&) = default;
    IDComponent(UUID id) : ID(id) {}

    void OnImGuiRender() {} // Non modifiable par l'utilisateur
};

struct PrefabComponent {
    std::string PrefabPath; // Le chemin du fichier .ceprefab d'origine

    PrefabComponent() = default;
    PrefabComponent(const PrefabComponent&) = default;
    PrefabComponent(const std::string& path) : PrefabPath(path) {}

    void OnImGuiRender() {} // Géré manuellement dans le panneau
};

struct MaterialComponent {
    std::string AssetPath;
    std::shared_ptr<Shader> ShaderInstance = nullptr;

    // --- TEXTURES CLASSIQUES (Du Parent) ---
    std::map<int, unsigned int> Textures;

    // --- NOUVEAU : OVERRIDES (De l'Instance) ---
    // Clé = Nom de l'uniform ("u_BaseColor").
    // Valeur = La data (on utilise des variables brutes pour simplifier)
    std::map<std::string, float> FloatOverrides;
    std::map<std::string, glm::vec4> ColorOverrides;
    std::map<std::string, bool> SwitchOverrides;
    std::map<std::string, unsigned int> TextureOverrides;

    MaterialComponent() = default;
    MaterialComponent(const MaterialComponent&) = default;
    MaterialComponent(const std::string& path) { SetAndCompile(path); }

    void SetAndCompile(const std::string& path) {
        AssetPath = path;

        // On nettoie l'état précédent
        Textures.clear();
        FloatOverrides.clear();
        ColorOverrides.clear();
        TextureOverrides.clear();

        std::ifstream file(path);
        if (!file.is_open()) return;

        nlohmann::json data;
        try { file >> data; } catch(...) { return; }

        std::filesystem::path filePath(path);

        // ==========================================
        // CAS 1 : C'EST UN MATERIAL (.cemat)
        // ==========================================
        if (filePath.extension() == ".cemat") {
            LoadMaterial(data, filePath);
        }
        // ==========================================
        // CAS 2 : C'EST UNE INSTANCE (.cematinst)
        // ==========================================
        else if (filePath.extension() == ".cematinst") {
            if (data.contains("Parent") && !data["Parent"].get<std::string>().empty()) {
                std::string parentPath = Project::GetProjectDirectory().string() + "/" + data["Parent"].get<std::string>();

                // 1. On charge la logique du Parent
                std::ifstream parentFile(parentPath);
                if (parentFile.is_open()) {
                    nlohmann::json parentData;
                    parentFile >> parentData;
                    LoadMaterial(parentData, parentPath);
                    parentFile.close();
                }

                // 2. On applique les Overrides par-dessus !
                if (data.contains("Overrides")) {
                    for (auto& [key, value] : data["Overrides"].items()) {
                        if (value.is_number()) {
                            FloatOverrides["u_" + key] = value.get<float>();
                        } else if (value.is_array() && value.size() == 4) {
                            ColorOverrides["u_" + key] = glm::vec4(value[0], value[1], value[2], value[3]);
                        } else if (value.is_string()) {
                            std::string texPath = value.get<std::string>();
                            if (!texPath.empty()) {
                                std::filesystem::path tp(texPath);
                                std::string fullPath = tp.is_absolute() ? tp.string() : (Project::GetProjectDirectory() / tp).string();
                                TextureOverrides["u_" + key] = TextureLoader::LoadTexture(fullPath.c_str());
                            }
                        }
                        else if (value.is_boolean()) {
                            SwitchOverrides[key] = value.get<bool>(); // <-- NOUVEAU
                        }
                    }
                }
            }
        }
    }

private:
    // Fonction utilitaire pour éviter de dupliquer le code de chargement du GLSL
    void LoadMaterial(const nlohmann::json& data, const std::filesystem::path& cematPath) {
        if (data.contains("GeneratedGLSL")) {
            std::string glsl = data["GeneratedGLSL"].get<std::string>();

            // --- INJECTION DES PERMUTATIONS ---
            std::string defines = "\n";
            if (data.contains("Nodes")) {
                for (auto& node : data["Nodes"]) {
                    if (node["Name"] == "StaticSwitchParameter") {
                        std::string paramName = node.value("ParameterName", "");
                        bool val = node.value("BoolValue", false);

                        // Override de l'instance si existant
                        if (SwitchOverrides.contains(paramName)) {
                            val = SwitchOverrides[paramName];
                        }
                        if (val && !paramName.empty()) {
                            defines += "#define " + paramName + "\n";
                        }
                    }
                }
            }

            size_t pos = glsl.find("#version");
            if (pos != std::string::npos) pos = glsl.find('\n', pos) + 1;
            else pos = 0;
            glsl.insert(pos, defines);

            // --- NOMMAGE UNIQUE DU SHADER ---
            std::filesystem::path cacheDir = Project::GetCacheDirectory();
            if (!std::filesystem::exists(cacheDir)) std::filesystem::create_directories(cacheDir);

            // Magie : L'instance aura son propre fichier avec son propre nom !
            std::string fragName = std::filesystem::path(AssetPath).stem().string() + ".frag";
            std::filesystem::path fragPath = cacheDir / fragName;

            std::ofstream outFrag(fragPath);
            outFrag << glsl;
            outFrag.close();

            ShaderInstance = std::make_shared<Shader>("shaders/default.vert", fragPath.string().c_str());
        }

        if (data.contains("Nodes")) {
            for (auto& node : data["Nodes"]) {
                if (node["Name"] == "Texture2D" && node.contains("TexturePath")) {
                    std::string texPath = node["TexturePath"].get<std::string>();
                    if (!texPath.empty()) {
                        int nodeID = node["ID"].get<int>();
                        std::filesystem::path tp(texPath);
                        // Si le chemin est déjà absolu, on l'utilise tel quel. Sinon, on ajoute le ProjectDir.
                        std::string fullPath = tp.is_absolute() ? tp.string() : (Project::GetProjectDirectory() / tp).string();
                        Textures[nodeID] = TextureLoader::LoadTexture(fullPath.c_str());
                    }
                }
            }
        }
    }

public:
    void OnImGuiRender() {
        if (!AssetPath.empty()) {
            ImGui::TextWrapped("Mat: %s", std::filesystem::path(AssetPath).filename().string().c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "No Material Assigned");
        }

        ImGui::Button("Drop .cemat/.cematinst", ImVec2(-1, 30));

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                std::filesystem::path filepath = (const char*)payload->Data;
                // --- ON ACCEPTE LES DEUX EXTENSIONS ! ---
                if (filepath.extension() == ".cemat" || filepath.extension() == ".cematinst") {
                    SetAndCompile(filepath.string());
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
};

// --- RÉFLEXION STATIQUE (Nouveau Standard) ---

// Cette liste permet à l'Inspector d'itérer automatiquement sur tous les types
// sans avoir à modifier manuellement Application.cpp à chaque nouveau composant.
using AllComponents = std::tuple<
    TagComponent,
    TransformComponent,
    ColorComponent,
    CameraComponent,
    MeshComponent,
    DirectionalLightComponent,
    RigidBodyComponent,
    BoxColliderComponent,
    NativeScriptComponent,
    RelationshipComponent,
    IDComponent,
    PrefabComponent,
    MaterialComponent
>;