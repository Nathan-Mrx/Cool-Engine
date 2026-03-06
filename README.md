# 🚀 Cool Engine (3D)

Cool Engine est un moteur de jeu 3D moderne développé en C++20, axé sur la performance, la modularité et un workflow orienté données. Conçu avec une approche Linux-first (optimisé pour CachyOS/Arch), il intègre des technologies standards de l'industrie pour offrir un environnement de développement professionnel et extensible.
## 🌟 Architecture & Core

Le cœur du moteur repose sur des bases solides et automatisées :

- Entity Component System (ECS) : Utilisation de EnTT pour une gestion ultra-performante des entités et des données.

- Gestion de Projet AAA : Structure de dossiers normalisée (Content, Binaries, Config) et support du chargement asynchrone des projets avec écran de démarrage.

- Métaprogrammation (CoolHeaderTool) : Un outil Python personnalisé (CHT) analyse le code source pour générer automatiquement les registres de scripts, de nœuds de matériaux et de types d'assets, éliminant ainsi le code de boilerplate manuel.

- Identification Unique : Système de UUID (Universally Unique Identifier) pour la gestion persistante des entités et des assets.

## 🎨 Rendu & Graphismes

Le pipeline de rendu exploite OpenGL 4.6 moderne :

- Modèle de Shading PBR : Implémentation complète du modèle de micro-facettes Cook-Torrance (Albedo, Metallic, Roughness, Normal Mapping, Ambient Occlusion).

- Éclairage Dynamique : Support des lumières directionnelles et ponctuelles.

- Post-Process & Debug : Système d'outline pour la sélection d'objets, rendu en fil de fer (wireframe) et grille de référence dynamique.

- Gestion des Shaders : Système de cache automatisé dans .ce_cache pour les shaders générés, évitant l'encombrement du navigateur de contenu.

## 🛠️ Éditeur de Matériaux (Nodal)

L'une des pièces maîtresses du moteur est son éditeur visuel :

- Graph de Matériau : Création de shaders complexes via une interface de nœuds intuitive (Maths, Textures, Couleurs, Reroute).

- Live Preview : Compilation en temps réel et prévisualisation sur une sphère 3D avec contrôle de la caméra et de la rotation.

- Hot-Reloading : Mise à jour instantanée de tous les matériaux dans la scène principale dès la sauvegarde, sans redémarrage.

- Système Wildcard : Adaptation dynamique du type des connecteurs (float vers vec3, etc.) pour une flexibilité maximale.

## 📂 Gestion d'Assets & UX

L'éditeur offre une expérience utilisateur fluide inspirée des standards du marché :

- Content Browser Evolué : Navigation par icônes, tri intelligent (Dossiers > Type > Nom), et masquage des extensions de fichiers.

- Asset Registry : Système extensible permettant de définir de nouveaux types d'assets (.cemat, .ceprefab, .cescene, .cewav) avec leurs propres couleurs et icônes via AssetDefinitions.h.

- Édition Directe : Renommage rapide via la touche F2 ou clic contextuel, tant pour les assets que pour les entités de la scène.

- Drag & Drop : Importation de modèles (.obj, .fbx) et assignation de matériaux par glisser-déposer.

- Viewport Interactif : Intégration de Gizmos (ImGuizmo) pour la manipulation directe des objets dans l'espace 3D.

## ⚖️ Physique & Gameplay

- Jolt Physics : Intégration avancée du moteur physique AAA pour la gestion des corps rigides (Statiques, Cinématiques, Dynamiques).

- Collisions : Boîtes de collision avec paramètres de friction et de restitution réglables.

- Native Scripting : Système de scripts C++ attachables aux entités, avec enregistrement automatique via le CoolHeaderTool.

## 🗺️ Roadmap

- [ ] Material Instances : Création de variantes de matériaux sans recompilation.

- [ ] Audio System : Intégration de la librairie FMOD ou SoLoud.

- [ ] Networking : Système de réseau pour les jeux multijoueurs. (dedicated game server avec protocole custom)

- [ ] UX : Ctrl+Z, Ctrl+Y, multi-sélection, duplication d'entités, etc.

- [ ] Cache : Meilleure gestion des fichiers temporaires

- [ ] Nodes : creer une liste de premade Nodes avec le UHT et retirer le bouton Add Component

- [ ] Physics : Integration plus avancee de Jolt Physics

- [ ] UI : Ajouter un theme, des icones et rendre l'UI plus intuitive et complete

- [ ] Documentation : Guide de l'utilisateur et tutoriels vidéo.

## 🏗️ Installation & Build (Linux)

Le moteur utilise vcpkg pour une gestion simplifiée des dépendances.
Bash

1. Cloner le dépôt et ses sous-modules


    git clone --recursive https://github.com/Nathan/CoolEngine.git
    cd CoolEngine

2. Installer les dépendances


    ./vcpkg/vcpkg install

3. Préparer les icônes (nécessite Python & Pillow)


    python3 tools/FormatIcons.py

4. Compiler avec CMake


    cmake -B build -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake
    cmake --build build -j $(nproc)

## 🛠️ Outils Inclus

- FormatIcons.py : Normalise, centre et blanchit automatiquement les icônes de l'UI pour un rendu uniforme.

- CoolHeaderTool.py : Automatise la réflexion C++ et le registre des classes.

---

Cool Engine est un projet en développement actif par Nathan.