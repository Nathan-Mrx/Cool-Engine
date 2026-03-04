#pragma once
#include "Entity.h"

class ScriptableEntity {
public:
    virtual ~ScriptableEntity() = default;

    // Raccourci ultra-pratique pour ne pas avoir à écrire m_Entity.GetComponent<T>() partout
    template<typename T>
    T& GetComponent() {
        return m_Entity.GetComponent<T>();
    }

    template<typename T>
    bool HasComponent() {
        return m_Entity.HasComponent<T>();
    }

    // Permet de chercher "Enfant", "Parent/Enfant", ou même "../Voisin"
    Entity GetNode(const std::string& path);

protected:
    // Ces fonctions seront surchargées par tes scripts de jeu (ex: PlayerController)
    virtual void OnCreate() {}
    virtual void OnUpdate(float ts) {}
    virtual void OnDestroy() {}

private:
    Entity m_Entity;
    
    // La Scène a le droit de modifier m_Entity en cachette quand elle instancie le script
    friend class Scene; 
};