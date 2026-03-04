#pragma once
#include "../scene/ScriptableNode.h"
#include <iostream>

#include "ScriptRegistry.h"

class RadarScript : public ScriptableNode {
private:
    Node m_AntennaNode; // On stocke le Node enfant ici !

public:
    void OnCreate() override {
        // 1. On cherche l'enfant nommé "Antenna"
        m_AntennaNode = GetNode("Antenna");

        // 2. Petit message de debug pour la console
        if (m_AntennaNode) {
            std::cout << "[RadarScript] Succes : L'antenne a ete trouvee !" << std::endl;
        } else {
            std::cerr << "[RadarScript] Erreur : Impossible de trouver le noeud 'Antenna' !" << std::endl;
        }
    }

    void OnUpdate(float ts) override {
        // 3. Si on a bien trouvé l'antenne, on la fait tourner !
        if (m_AntennaNode) {
            auto& transform = m_AntennaNode.GetComponent<TransformComponent>();
            transform.RotationEuler.y += 90.0f * ts; // Tourne de 90 degrés par seconde
        }
    }
};

REGISTER_SCRIPT(RadarScript)