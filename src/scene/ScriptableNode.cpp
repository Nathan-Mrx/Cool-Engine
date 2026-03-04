#include "ScriptableNode.h"
#include "../ecs/Components.h"
#include <sstream>
#include <vector>

Node ScriptableNode::GetNode(const std::string& path) {
    if (path.empty() || !m_Node) return {}; // On utilise m_Node

    std::stringstream ss(path);
    std::string segment;
    std::vector<std::string> segments;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) segments.push_back(segment);
    }

    Node currentNode = m_Node; // On utilise m_Node

    for (const auto& seg : segments) {
        if (seg == "..") {
            if (currentNode.HasComponent<RelationshipComponent>()) {
                entt::entity parentID = currentNode.GetComponent<RelationshipComponent>().Parent;
                if (parentID != entt::null) {
                    currentNode = Node{ parentID, currentNode.GetScene() };
                    continue;
                }
            }
            return {};
        }

        bool found = false;
        if (currentNode.HasComponent<RelationshipComponent>()) {
            entt::entity childID = currentNode.GetComponent<RelationshipComponent>().FirstChild;
            while (childID != entt::null) {
                Node child{ childID, currentNode.GetScene() };
                if (child.HasComponent<TagComponent>() && child.GetComponent<TagComponent>().Tag == seg) {
                    currentNode = child;
                    found = true;
                    break;
                }
                childID = child.GetComponent<RelationshipComponent>().NextSibling;
            }
        }
        if (!found) return {};
    }
    return currentNode;
}