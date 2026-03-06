#pragma once
#include <functional>
#include <vector>

// La classe générique de Multicast Delegate
template<typename... Args>
class MulticastDelegate {
private:
    using CallbackType = std::function<void(Args...)>;
    std::vector<CallbackType> m_Callbacks;

public:
    // --- L'équivalent de AddRaw / AddDynamic d'Unreal ---
    // Permet de lier une fonction membre d'une classe spécifique
    template<class T>
    void AddRaw(T* instance, void (T::*memberFunction)(Args...)) {
        m_Callbacks.push_back([instance, memberFunction](Args... params) {
            (instance->*memberFunction)(params...);
        });
    }

    // --- L'équivalent de AddLambda d'Unreal ---
    // Permet de lier une fonction anonyme directement dans le code
    void AddLambda(CallbackType lambda) {
        m_Callbacks.push_back(lambda);
    }

    // --- L'équivalent de Broadcast d'Unreal ---
    // Appelle toutes les fonctions enregistrées avec les paramètres donnés
    void Broadcast(Args... args) const {
        for (const auto& callback : m_Callbacks) {
            if (callback) {
                callback(args...);
            }
        }
    }

    // Vide la liste des listeners
    void Clear() {
        m_Callbacks.clear();
    }
};

// =====================================================================
// LES MACROS FAÇON UNREAL ENGINE
// Elles masquent les templates complexes pour rendre le code lisible !
// =====================================================================

#define DECLARE_MULTICAST_DELEGATE(DelegateName) \
    using DelegateName = MulticastDelegate<>;

#define DECLARE_MULTICAST_DELEGATE_OneParam(DelegateName, Param1Type) \
    using DelegateName = MulticastDelegate<Param1Type>;

#define DECLARE_MULTICAST_DELEGATE_TwoParams(DelegateName, Param1Type, Param2Type) \
    using DelegateName = MulticastDelegate<Param1Type, Param2Type>;

#define DECLARE_MULTICAST_DELEGATE_ThreeParams(DelegateName, Param1Type, Param2Type, Param3Type) \
    using DelegateName = MulticastDelegate<Param1Type, Param2Type, Param3Type>;
