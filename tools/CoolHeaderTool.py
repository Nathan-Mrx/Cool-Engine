import os
import sys

def generate_script_registry(project_source_dir, output_file):
    includes = []

    # On parcourt le dossier Source/ du PROJET
    if os.path.exists(project_source_dir):
        for root, _, files in os.walk(project_source_dir):
            for file in files:
                if file.endswith(".h") or file.endswith(".hpp"):
                    filepath = os.path.join(root, file)

                    with open(filepath, 'r', encoding='utf-8') as f:
                        content = f.read()
                        if "REGISTER_SCRIPT" in content:
                            # Calcul du chemin relatif par rapport au fichier généré
                            rel_path = os.path.relpath(filepath, os.path.dirname(output_file))
                            rel_path = rel_path.replace('\\', '/')
                            includes.append(f'#include "{rel_path}"')

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("// ==================================================================\n")
        f.write("// Fichier généré automatiquement par le Cool Header Tool (CHT)\n")
        f.write("// NE PAS MODIFIER ! Module de liaison pour le Projet.\n")
        f.write("// ==================================================================\n\n")

        # Ce define permet d'éviter des conflits d'exports/imports de DLL/so plus tard
        f.write("#define COOL_ENGINE_GAME_MODULE\n\n")

        if not includes:
            f.write("// Aucun script trouve dans le projet.\n")
        else:
            for inc in includes:
                f.write(inc + "\n")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python CoolHeaderTool.py <ProjectSourceDir> <OutputCppFile>")
        sys.exit(1)

    generate_script_registry(sys.argv[1], sys.argv[2])