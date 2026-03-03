# 🚀 Cool Engine (3D)

**Cool Engine** est un moteur de jeu 3D "Industry-Ready" développé en C++20, conçu avec une architecture **Linux-first**. Ce projet est né de la volonté de créer une plateforme performante, modulaire et hautement personnalisable, en s'appuyant sur les standards modernes de l'industrie comme l'ECS (Entity Component System) et le multi-threading.

## 🛠 Tech Stack & Vitals

Le moteur repose sur une sélection de bibliothèques de pointe pour garantir stabilité et performance sur les systèmes Arch-based (CachyOS) :

* **Core Engine :** C++20, CMake, vcpkg.
* **Architecture :** [EnTT](https://github.com/skypjack/entt) (Entity Component System) pour une gestion de données ultra-rapide.
* **Graphismes :** OpenGL 4.6 (Modern Pipeline), GLM (Maths).
* **Physique :** [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (Moteur AAA utilisé dans *Horizon Forbidden West*).
* **UI & Editor :** Dear ImGui (Docking branch), ImGuizmo pour les manipulateurs 3D.
* **Asset Loading :** Assimp pour le chargement de modèles 3D complexes (.obj, .fbx).
* **Sérialisation :** nlohmann/json pour la sauvegarde des scènes au format `.cescene`.

---

## ✨ Fonctionnalités Actuelles

### 🖥️ Éditeur Professionnel

* **Scene Hierarchy :** Gestion intuitive des entités avec création de primitives (Cubes, Sphères, Planes) générées procéduralement.
* **Inspector Dynamique :** Réflexion statique permettant de modifier les composants (Transform, Color, Mesh, Light) en temps réel.
* **3D Gizmos :** Manipulation visuelle des objets avec les raccourcis standards de l'industrie (`W`, `E`, `R` pour Translate, Rotate, Scale).
* **Viewport Interactif :** Rendu déporté via Framebuffers avec support du Drag & Drop d'assets directement depuis l'explorateur.

### 📂 Gestion de Projet & Assets

* **Content Browser :** Navigation dans les dossiers du projet avec rendu par "cards" et glisser-déposer vers le Viewport ou l'Inspector.
* **Sérialisation JSON :** Système complet de sauvegarde et de chargement des niveaux via des boîtes de dialogue natives (NFD).

### 💡 Rendu & Physique

* **Lumière Directionnelle :** Gestion des ombres et de l'éclairage diffus/ambiant via des shaders GLSL modernes.
* **Intégration Jolt :** (En cours) Simulation physique haute performance pour les RigidBodies et les Colliders.

---

## 🏗️ Installation & Build (Linux / CachyOS)

Le moteur utilise **vcpkg** pour une gestion sans douleur des dépendances.

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

## 🗺️ Roadmap

* [ ] **Physique :** Finaliser l'intégration des RigidBodies Jolt.
* [ ] **Play Mode :** Ajouter un bouton Play/Stop pour tester la scène sans quitter l'éditeur.
* [ ] **Scripting :** Système de "Native Scripting" en C++ pour la logique de gameplay.
* [ ] **Networking :** Protocole personnalisé pour le multijoueur en ligne.

---

*Développé par Nathan Merieux*