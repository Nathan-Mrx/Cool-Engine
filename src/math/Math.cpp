#include "Math.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Math {
    bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale) {
        // glm::decompose a besoin de ces variables même si on ne s'en sert pas
        glm::vec3 skew;
        glm::vec4 perspective;
        
        // Décompose la matrice en ses composants de base
        return glm::decompose(transform, scale, rotation, translation, skew, perspective);
    }
}