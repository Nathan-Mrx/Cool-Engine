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
    else:
        print("Mode inconnu. Utilisez --scripts ou --materials")