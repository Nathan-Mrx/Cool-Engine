cmake_minimum_required(VERSION 3.20)
project(GameEngine)

set(CMAKE_CXX_STANDARD 20)

# Trouver les paquets fournis par vcpkg
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(NFD CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(ImGuizmo CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)

# Lister tous les fichiers sources du moteur
set(SOURCES
        src/main.cpp
        src/core/Application.cpp
        src/renderer/Shader.cpp
        src/ecs/Components.h
        src/renderer/Framebuffer.cpp
        src/core/Input.cpp
        src/project/Project.cpp
        src/renderer/Mesh.cpp
        src/renderer/ModelLoader.h
        src/editor/panels/ContentBrowserPanel.cpp
        src/editor/panels/ContentBrowserPanel.h
        src/scene/SceneSerializer.cpp
        src/scene/SceneSerializer.h
        src/scene/Scene.cpp
        src/scene/Scene.h
        src/scene/Entity.cpp
        src/scene/Entity.h
        src/renderer/Renderer.cpp
        src/renderer/Renderer.h
        src/editor/EditorLayer.cpp
        src/editor/EditorLayer.h
        src/editor/panels/SceneHierarchyPanel.cpp
        src/editor/panels/SceneHierarchyPanel.h
        src/editor/panels/HubPanel.cpp
        src/editor/panels/HubPanel.h
        src/renderer/PrimitiveFactory.cpp
        src/renderer/PrimitiveFactory.h
        src/renderer/TextureLoader.h
        src/physics/PhysicsEngine.cpp
        src/physics/PhysicsEngine.h
        src/scene/ScriptableNode.cpp
        src/scene/ScriptableNode.h
        src/scripts/ScriptRegistry.h
        src/core/UUID.cpp
        src/core/UUID.h
        src/math/Math.cpp
        src/math/Math.h
        src/core/Delegate.cpp
        src/core/Delegate.h
        src/project/ProjectCompiler.cpp
        src/project/ProjectCompiler.h
        src/renderer/stb_image_impl.cpp
        src/editor/panels/MaterialEditorPanel.cpp
        src/editor/panels/MaterialEditorPanel.h
        src/editor/panels/MaterialInstanceEditorPanel.cpp
        src/editor/panels/MaterialInstanceEditorPanel.h
        src/editor/materials/MaterialGraph.h
        src/editor/materials/MaterialNodeRegistry.h
        src/editor/materials/NodeDefinitions.h
        src/editor/materials/MaterialNodeRegistry.generated.cpp
        src/editor/IAssetEditor.h
        src/editor/AssetRegistry.h
        src/editor/AssetDefinitions.h
        src/editor/AssetRegistry.generated.cpp
        src/editor/UndoManager.cpp
        src/editor/UndoManager.h
        src/editor/EditorCommands.cpp
        src/editor/EditorCommands.h
)

# --- COOL HEADER TOOL (Génération de code pré-build) ---
set(MATERIAL_REGISTRY_OUT "${CMAKE_SOURCE_DIR}/src/editor/materials/MaterialNodeRegistry.generated.cpp")

add_custom_command(
        OUTPUT ${MATERIAL_REGISTRY_OUT}
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/CoolHeaderTool.py --materials ${CMAKE_SOURCE_DIR}/src/editor/materials ${MATERIAL_REGISTRY_OUT}
        DEPENDS ${CMAKE_SOURCE_DIR}/src/editor/materials/NodeDefinitions.h
        COMMENT "CoolHeaderTool: Generation du registre des noeuds de materiaux..."
)

# --- GÉNÉRATION DU REGISTRE D'ASSETS ---
set(ASSET_REGISTRY_OUT "${CMAKE_SOURCE_DIR}/src/editor/AssetRegistry.generated.cpp")

add_custom_command(
        OUTPUT ${ASSET_REGISTRY_OUT}
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/CoolHeaderTool.py --assets ${CMAKE_SOURCE_DIR}/src/editor ${ASSET_REGISTRY_OUT}
        DEPENDS ${CMAKE_SOURCE_DIR}/src/editor/AssetDefinitions.h
        COMMENT "CoolHeaderTool: Generation du registre des assets..."
)


# Créer l'exécutable avec les sources
add_executable(GameEngine ${SOURCES} ${ASSET_REGISTRY_OUT})

# Permettre d'inclure les headers depuis le dossier src/ facilement
target_include_directories(GameEngine PRIVATE src)

# Lier les bibliothèques
target_link_libraries(GameEngine PRIVATE
        glfw
        glad::glad
        glm::glm
        EnTT::EnTT
        imgui::imgui
        nlohmann_json::nlohmann_json
        nfd::nfd
        assimp::assimp
        imguizmo::imguizmo
        unofficial::imgui-node-editor::imgui-node-editor
        Jolt::Jolt)

# Copier automatiquement le dossier shaders à côté de l'exécutable après la compilation
add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:GameEngine>/shaders
        COMMENT "Copie des shaders vers le dossier de build..."
)

# On copie l'icône à côté de l'exécutable
add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/splash.png
        $<TARGET_FILE_DIR:GameEngine>/splash.png
        COMMENT "Copie de l'icône vers le dossier de build..."
)

# Copier automatiquement le dossier d'icônes à côté de l'exécutable après la compilation
add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/icons
        $<TARGET_FILE_DIR:GameEngine>/icons
        COMMENT "Copie des icones vers le dossier de build..."
)

# Jolt nécessite quelques définitions de préprocesseur pour fonctionner correctement
target_compile_definitions(${PROJECT_NAME} PRIVATE
        #JPH_DEBUG_RENDERER         # Pour pouvoir afficher les boîtes de collision plus tard
        #JPH_PROFILE_ENABLED        # Pour le profilage
        #JPH_FLOATING_POINT_EXCEPTIONS_ENABLED
)

# --- MODULES DYNAMIQUES ---
# 1. On force l'exécutable à exporter ses symboles pour le .so
set_target_properties(GameEngine PROPERTIES ENABLE_EXPORTS TRUE)
target_link_options(GameEngine PRIVATE "-rdynamic")

# 2. On lie la librairie standard Linux pour dlopen/dlclose
target_link_libraries(GameEngine PRIVATE ${CMAKE_DL_LIBS})