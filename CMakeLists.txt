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
        src/editor/ContentBrowserPanel.cpp
        src/editor/ContentBrowserPanel.h
        src/scene/SceneSerializer.cpp
        src/scene/SceneSerializer.h
)

# Créer l'exécutable avec les sources
add_executable(GameEngine ${SOURCES})

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
        assimp::assimp)

# Copier automatiquement le dossier shaders à côté de l'exécutable après la compilation
add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:GameEngine>/shaders
        COMMENT "Copie des shaders vers le dossier de build..."
)