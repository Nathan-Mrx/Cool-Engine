#pragma once

#include <string>

class ShaderCompiler {
public:
    // Compiles a GLSL source file to SPIR-V using 'glslc' installed on the system.
    // Returns true if successful, false otherwise with the error log populated.
    static bool CompileGLSLToSPIRV(const std::string& glslPath, const std::string& spvOutPath, std::string& outErrorLog);
};
