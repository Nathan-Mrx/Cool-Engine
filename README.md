# 🚀 Cool Engine (3D)

**Cool Engine** est un moteur de jeu 3D développé en C++20, conçu avec une architecture "Industry-Ready" orientée performance et modularité. Pensé avec une approche **Linux-first** (idéal pour Arch/CachyOS), il repose sur les standards modernes de l'industrie du jeu vidéo.

## 🌟 Présentation du Projet

Le but de Cool Engine est d'offrir une plateforme de création visuelle performante et un environnement de développement optimisé. À la croisée entre la puissance d'un moteur C++ bas niveau et la flexibilité d'un éditeur moderne, le moteur s'articule autour de concepts clés :

* **Entity Component System (ECS) :** Architecture de traitement ultra-rapide basée sur [EnTT](https://github.com/skypjack/entt) pour la gestion optimisée des données en jeu.
* **Éditeur Intégré :** Un éditeur professionnel complet offrant une hiérarchie dynamique (`ScriptableNode`), un système d'Inspector temps réel via réflexion statique, et des modificateurs interactifs (Gizmos 3D).
* **Rendu & Physique :** Un rendu basé sur OpenGL 4.6 (Modern Pipeline) couplé à une implémentation grandissante du moteur physique AAA [Jolt Physics](https://github.com/jrouwe/JoltPhysics).
* **Workflow Avancé :** Support du rechargement à chaud (Hot-Reloading), d'une compilation asynchrone pour les projets avec écrans de démarrage, et d'une conception par modules dynamiques. Un éditeur de matériaux par nœuds est également de la partie.
* **Gestion d'Assets :** Un navigateur de contenu ("Content Browser") avec glisser-déposer, assurant une sérialisation complète des scènes (.cescene) au format JSON.

---

## 🗺️ Roadmap

L'objectif est d'étendre progressivement les capacités du moteur pour en faire une solution toujours plus polyvalente. Voici les prochaines étapes majeures de développement :

* [ ] **Éditeur de Matériaux :** Finaliser et étendre l'éditeur de matériaux basé sur des nœuds (Material node based editor).
* [ ] **Physique :** Intégration plus poussée de Jolt Physics (Deeper Jolt physics integration).
* [ ] **Audio :** Intégration du middleware audio FMOD.
* [ ] **Input :** Implémenter un système d'entrées multi-appareils (Multi-device input system).
* [ ] **Play Mode :** Ajouter un bouton Play/Stop pour tester la scène sans quitter l'éditeur.
* [ ] **Scripting :** Système de "Native Scripting" en C++ pour la logique de gameplay.
* [ ] **Networking :** Protocole personnalisé pour le multijoueur en ligne.

---

## 🏗️ Installation & Build (Linux / CachyOS)

La gestion des dépendances du moteur est gérée sans heurts par **vcpkg**.

```bash
# 1. Cloner le dépôt
git clone https://github.com/Nathan/CoolEngine.git
cd CoolEngine

# 2. Installer les dépendances via vcpkg
./vcpkg/vcpkg install

# 3. Configurer et compiler avec CMake
cmake -B build -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)
```

---
*Développé par Nathan Merieux*