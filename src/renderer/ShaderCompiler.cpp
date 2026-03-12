#include "ShaderCompiler.h"
#include <iostream>
#include <array>
#include <stdexcept>
#include <memory>
#include <cstdio>
#include <filesystem>

bool ShaderCompiler::CompileGLSLToSPIRV(const std::string& glslPath, const std::string& spvOutPath, std::string& outErrorLog) {
    if (!std::filesystem::exists(glslPath)) {
        outErrorLog = "Source GLSL file does not exist: " + glslPath;
        return false;
    }

    std::string command = "glslc \"" + glslPath + "\" -o \"" + spvOutPath + "\" 2>&1";

    std::array<char, 256> buffer;
    outErrorLog.clear();

    // Ouvrir un pipe au processus glslc
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        outErrorLog = "popen() failed! Could not start glslc.";
        return false;
    }

    // Lire la sortie console de glslc
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        outErrorLog += buffer.data();
    }

    // Fermer le pipe et vérifier le code de retour
    int returnCode = pclose(pipe.release());
    
    // Le compilateur ShaderC/glslc retourne 0 en cas de succès absolu
    return (returnCode == 0);
}
