#pragma once
#include <string>
#include "Entity.h"

class ScriptableNode {
public:
    virtual ~ScriptableNode() = default;

    template<typename T>
    T& GetComponent() { return m_Node.GetComponent<T>(); }

    template<typename T>
    bool HasComponent() { return m_Node.HasComponent<T>(); }

    virtual void OnCreate() {}
    virtual void OnDestroy() {}
    virtual void OnUpdate(float ts) {}

    Node GetNode(const std::string& path);

protected:
    Node m_Node; // Anciennement m_Node
    friend class Scene; 
};
