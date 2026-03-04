#include "ProjectCompiler.h"
#include <stdio.h>
#include <iostream>

void ProjectCompiler::Start(const std::filesystem::path& projectDir) {
    if (s_IsCompiling) return;
    
    Reset(); // Nettoie le précédent thread s'il y en avait un
    
    s_IsCompiling = true;
    s_HasFinished = false;
    s_Result = false;
    s_Logs.clear();

    s_CompilerThread = std::thread(CompileThread, projectDir);
}

void ProjectCompiler::CompileThread(std::filesystem::path projectDir) {
    auto addLog = [](const std::string& log) {
        std::lock_guard<std::mutex> lock(s_LogMutex);
        s_Logs.push_back(log);
    };

    addLog("[Compiler] Début de la compilation pour le projet : " + projectDir.string());

    std::string buildDir = (projectDir / "cmake-build-debug").string(); // ou "Binaries/build"
    
    // 1. Configuration CMake (Le 2>&1 permet de capturer les erreurs !)
    std::string cmdConfig = "cmake -B \"" + buildDir + "\" -S \"" + projectDir.string() + "\" 2>&1";
    addLog("> " + cmdConfig);
    
    FILE* pipe = popen(cmdConfig.c_str(), "r");
    char buffer[256];
    if (pipe) {
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            addLog(std::string(buffer));
        }
        pclose(pipe);
    }

    // 2. Compilation (Build)
    std::string cmdBuild = "cmake --build \"" + buildDir + "\" --target GameModule -j 12 2>&1";
    addLog("> " + cmdBuild);

    pipe = popen(cmdBuild.c_str(), "r");
    if (!pipe) {
        addLog("[Compiler] ERREUR FATALE : Impossible de lancer CMake.");
        s_IsCompiling = false; s_HasFinished = true; return;
    }

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        addLog(std::string(buffer));
    }
    int buildStatus = pclose(pipe);

    if (buildStatus == 0) {
        addLog("[Compiler] SUCCES : Le module du jeu a ete compile !");
        s_Result = true;
    } else {
        addLog("[Compiler] ECHEC : Erreur de compilation C++.");
        s_Result = false;
    }

    s_IsCompiling = false;
    s_HasFinished = true;
}

bool ProjectCompiler::IsCompiling() { return s_IsCompiling; }
bool ProjectCompiler::HasFinished() { return s_HasFinished; }
bool ProjectCompiler::GetResult() { return s_Result; }

std::vector<std::string> ProjectCompiler::GetLogs() {
    std::lock_guard<std::mutex> lock(s_LogMutex);
    return s_Logs;
}

void ProjectCompiler::Reset() {
    if (s_CompilerThread.joinable()) {
        s_CompilerThread.join();
    }
    s_IsCompiling = false;
    s_HasFinished = false;
    s_Result = false;
}
