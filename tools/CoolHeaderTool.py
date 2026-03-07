import os
import sys
import re

def generate_script_registry(project_source_dir, output_file):
    # ... (Ton code existant pour les scripts du projet) ...
    includes = []
    if os.path.exists(project_source_dir):
        for root, _, files in os.walk(project_source_dir):
            for file in files:
                if file.endswith(".h") or file.endswith(".hpp"):
                    filepath = os.path.join(root, file)
                    with open(filepath, 'r', encoding='utf-8') as f:
                        content = f.read()
                        if "REGISTER_SCRIPT" in content:
                            rel_path = os.path.relpath(filepath, os.path.dirname(output_file)).replace('\\', '/')
                            includes.append(f'#include "{rel_path}"')

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("// Fichier généré automatiquement par le Cool Header Tool (CHT)\n")
        f.write("#define COOL_ENGINE_GAME_MODULE\n\n")
        if not includes: f.write("// Aucun script trouve.\n")
        else:
            for inc in includes: f.write(inc + "\n")

# --- NOUVEAU : Le générateur pour les Noeuds de Matériaux ! ---
def generate_material_registry(engine_source_dir, output_file):
    includes = set() # Set pour éviter les doublons
    nodes = []

    # Regex pour trouver exactement : CEMAT_NODE(NomDeLaClasse)
    regex = re.compile(r'CEMAT_NODE\(\)\s*(?:struct|class)\s+([A-Za-z0-9_]+)')

    if os.path.exists(engine_source_dir):
        for root, _, files in os.walk(engine_source_dir):
            for file in files:
                if file.endswith(".h") or file.endswith(".hpp"):
                    filepath = os.path.join(root, file)
                    with open(filepath, 'r', encoding='utf-8') as f:
                        content = f.read()
                        matches = regex.findall(content)
                        if matches:
                            rel_path = os.path.relpath(filepath, os.path.dirname(output_file)).replace('\\', '/')
                            includes.add(f'#include "{rel_path}"')
                            nodes.extend(matches)

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("// ==================================================================\n")
        f.write("// Fichier généré automatiquement par le Cool Header Tool (CHT)\n")
        f.write("// NE PAS MODIFIER MANUELLEMENT !\n")
        f.write("// ==================================================================\n\n")

        f.write('#include "MaterialNodeRegistry.h"\n')
        for inc in includes:
            f.write(inc + "\n")

        f.write("\nvoid MaterialNodeRegistry::RegisterAllNodes() {\n")
        f.write("    static bool s_Initialized = false;\n")
        f.write("    if (s_Initialized) return;\n\n")

        for node in nodes:
            f.write(f"    Register(std::make_shared<{node}>());\n")

        f.write("\n    s_Initialized = true;\n")
        f.write("}\n")

# --- NOUVEAU : Le générateur pour les Assets ! ---
def generate_asset_registry(engine_source_dir, output_file):
    includes = set()
    assets = []

    # Regex pour trouver : CE_ASSET(NomDeLaClasse)
    regex = re.compile(r'CE_ASSET\(\)\s*(?:struct|class)\s+([A-Za-z0-9_]+)')

    if os.path.exists(engine_source_dir):
        for root, _, files in os.walk(engine_source_dir):
            for file in files:
                if file.endswith(".h") or file.endswith(".hpp"):
                    filepath = os.path.join(root, file)
                    with open(filepath, 'r', encoding='utf-8') as f:
                        content = f.read()
                        matches = regex.findall(content)
                        if matches:
                            rel_path = os.path.relpath(filepath, os.path.dirname(output_file)).replace('\\', '/')
                            includes.add(f'#include "{rel_path}"')
                            assets.extend(matches)

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("// ==================================================================\n")
        f.write("// Fichier généré automatiquement par le Cool Header Tool (CHT)\n")
        f.write("// ==================================================================\n\n")

        f.write('#include "AssetRegistry.h"\n')
        for inc in includes:
            f.write(inc + "\n")

        f.write("\nvoid AssetRegistry::RegisterAllAssets() {\n")
        f.write("    static bool s_Initialized = false;\n")
        f.write("    if (s_Initialized) return;\n\n")

        for asset in assets:
            f.write(f"    Register(std::make_shared<{asset}>());\n")

        f.write("\n    s_Initialized = true;\n")
        f.write("}\n")

# ... (Dans le bloc if __name__ == "__main__": en bas du fichier) ...
if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python CoolHeaderTool.py [--scripts | --materials | --assets] <SourceDir> <OutputCppFile>")
        sys.exit(1)

    mode = sys.argv[1]
    source_dir = sys.argv[2]
    output_file = sys.argv[3]

    if mode == "--scripts":
        generate_script_registry(source_dir, output_file)
    elif mode == "--materials":
        generate_material_registry(source_dir, output_file)
    elif mode == "--assets": # <-- LA NOUVELLE COMMANDE
        generate_asset_registry(source_dir, output_file)
    else:
        print("Mode inconnu. Utilisez --scripts, --materials ou --assets")

# --- NOUVEAU : Le générateur pour les Composants (ECS) ---
def generate_component_registry(engine_source_dir, output_file):
    components = {} # Dictionnaire { "TransformComponent" : ["Location", "RotationEuler", "Scale"] }

    if os.path.exists(engine_source_dir):
        for root, _, files in os.walk(engine_source_dir):
            for file in files:
                if file.endswith(".h") or file.endswith(".hpp"):
                    filepath = os.path.join(root, file)
                    with open(filepath, 'r', encoding='utf-8') as f:
                        lines = f.readlines()

                    current_comp = None
                    for i, line in enumerate(lines):
                        if "CE_COMPONENT(" in line:
                            # Cherche la déclaration de la struct/class sur la ligne courante ou la suivante
                            for j in range(0, 3):
                                if i+j < len(lines):
                                    m = re.search(r'(?:struct|class)\s+([A-Za-z0-9_]+)', lines[i+j])
                                    if m:
                                        current_comp = m.group(1)
                                        components[current_comp] = []
                                        break
                        elif current_comp and "CE_PROPERTY(" in line:
                            # Extrait le nom de la variable juste avant le '=' ou le ';'
                            cleaned = line.split('//')[0]
                            m = re.search(r'([A-Za-z0-9_]+)\s*(?:=.*)?\s*;', cleaned)
                            if m:
                                components[current_comp].append(m.group(1))

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("// ==================================================================\n")
        f.write("// Fichier genere automatiquement par le Cool Header Tool (CHT)\n")
        f.write("// NE PAS MODIFIER MANUELLEMENT !\n")
        f.write("// ==================================================================\n\n")
        f.write("#pragma once\n")
        f.write("#include <tuple>\n")
        f.write("#include <nlohmann/json.hpp>\n\n")

        f.write("// --- SERIALISATION AUTOMATIQUE ---\n")
        for comp, props in components.items():
            if len(props) > 0:
                props_str = ", ".join(props)
                f.write(f"NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE({comp}, {props_str})\n")
            else:
                # Si un composant n'a pas de CE_PROPERTY, on génère des sérialiseurs vides
                f.write(f"inline void to_json(nlohmann::json& j, const {comp}& t) {{}}\n")
                f.write(f"inline void from_json(const nlohmann::json& j, {comp}& t) {{}}\n")

        f.write("\n// --- TUPLE DE TOUS LES COMPOSANTS ---\n")
        comps_list = ", ".join(components.keys())
        f.write(f"using AllComponents = std::tuple<{comps_list}>;\n\n")

        f.write("// --- REFLEXION DES NOMS ---\n")
        f.write("template <typename T> constexpr const char* GetComponentName() { return \"Unknown\"; }\n")
        for comp in components.keys():
            # Génère un string propre en retirant "Component" à la fin si on veut
            display_name = comp.replace("Component", "") if comp != "TagComponent" else "Tag"
            f.write(f"template <> constexpr const char* GetComponentName<{comp}>() {{ return \"{display_name}\"; }}\n")
        f.write("\n")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python CoolHeaderTool.py [--scripts | --materials] <SourceDir> <OutputCppFile>")
        sys.exit(1)

    mode = sys.argv[1]
    source_dir = sys.argv[2]
    output_file = sys.argv[3]

    if mode == "--scripts":
        generate_script_registry(source_dir, output_file)
    elif mode == "--materials":
        generate_material_registry(source_dir, output_file)
    elif mode == "--assets":
        generate_asset_registry(source_dir, output_file)
    elif mode == "--components":
        generate_component_registry(source_dir, output_file)
    else:
        print("Mode inconnu. Utilisez --scripts ou --materials")